#include "checker.hpp"

Checker::Checker(Arena& arena, Module& mod)
: arena(arena)
, mod(mod)
, src_file(nullptr)
, enclosing_return_type(nullptr)
, loop_depth(0)
{
    if (mod.deps.size() > 0) {
        // Core module is always the last dependency added to any module. All
        // modules depend on it implicitly (even if they don't use any symbols
        // from it).
        core_dep = &mod.deps.back();
    } else {
        core_dep = nullptr;
    }
}

void Checker::CheckModule() {
    resolveNamedTypes();

    for (auto& sfile : mod.files) {
        src_file = &sfile;

        for (auto* def : sfile.defs) {
            checkDef(def);
        }
    }
}

/* -------------------------------------------------------------------------- */

static bool resolveNamedInDep(Type* type, Module::Dependency& dep) {
    auto& named = type->ty_Named;

    auto it = dep.mod->symbol_table.find(named.name);
    if (it != dep.mod->symbol_table.end() && it->second->export_num != UNEXPORTED) {
        named.mod_id = dep.mod->id;
        named.mod_name = dep.mod->name;
        named.type = it->second->type->ty_Named.type;

        dep.usages.insert(it->second->export_num);

        return true;
    }

    return false;
}

void Checker::resolveNamedTypes() {
    for (auto& pair : mod.named_table.internal_refs) {
        auto& ref = pair.second;
        auto& named = ref.named_type->ty_Named;

        auto it = mod.symbol_table.find(named.name);
        if (it != mod.symbol_table.end()) {
            named.mod_id = mod.id;
            named.mod_name = mod.name;
            named.type = it->second->type->ty_Named.type;
            continue;
        }

        if (!resolveNamedInDep(ref.named_type, *core_dep)) {
            for (auto& span : ref.spans) {
                error(span, "undefined symbol: {}", named.name);
            }
        }        
    }

    for (size_t dep_id = 0; dep_id < mod.named_table.external_refs.size(); dep_id++) {
        auto& dep = mod.deps[dep_id];

        for (auto& pair : mod.named_table.external_refs[dep_id]) {
            auto* named_type = pair.second.named_type;
            
            if (!resolveNamedInDep(named_type, dep)) {
                for (auto& span : pair.second.spans) {
                    error(span, "module {} has no exported symbol named {}", dep.mod->name, named_type->ty_Named.name);
                }
            }
        }
    }


    if (ErrorCount()) {
        throw CompileError{};
    }
}

/* -------------------------------------------------------------------------- */

void Checker::mustEqual(const TextSpan &span, Type* a, Type* b) {
    tctx.flags |= TC_INFER;

    if (!tctx.Equal(a, b)) {
        fatal(span, "type mismatch: {} v. {}", a->ToString(), b->ToString());
    }

    tctx.flags ^= TC_INFER;
}

void Checker::mustSubType(const TextSpan &span, Type* sub, Type* super) {
    tctx.flags |= TC_INFER;

    if (!tctx.SubType(sub, super)) {
        fatal(span, "{} is not a subtype of {}", sub->ToString(), super->ToString());
    }

    tctx.flags ^= TC_INFER;
}

void Checker::mustCast(const TextSpan &span, Type* src, Type* dest) {
    tctx.flags |= TC_INFER;

    if (!tctx.Cast(src, dest)) {
        fatal(span, "{} cannot be cast to {}", src->ToString(), dest->ToString());
    }

    tctx.flags ^= TC_INFER;
}

void Checker::mustIntType(const TextSpan& span, Type* type) {
    tctx.flags |= TC_INFER;

    if (!tctx.IsIntType(type)) {
        fatal(span, "expected an integer type but got {}", type->ToString());
    }

    tctx.flags ^= TC_INFER;
}

Type* Checker::newUntyped(UntypedKind kind) {
    auto* untyped = AllocType(arena, TYPE_UNTYP);
    tctx.AddUntyped(untyped, kind);
    return untyped;
}

/* -------------------------------------------------------------------------- */

void Checker::declareLocal(Symbol* sym) {
    Assert(scope_stack.size() > 0, "declare local on empty scope stack");

    auto& curr_scope = scope_stack.back();

    auto it = curr_scope.find(sym->name);
    if (it == curr_scope.end()) {
        curr_scope[sym->name] = sym;
    } else {
        fatal(sym->span, "multiple definitions of local variable {} in the same scope", sym->name);
    }
}

void Checker::pushScope() {
    scope_stack.push_back({});
}

void Checker::popScope() {
    Assert(scope_stack.size() > 0, "pop on empty scope stack");

    scope_stack.pop_back();
}

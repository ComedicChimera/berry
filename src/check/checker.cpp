#include "checker.hpp"

Checker::Checker(Arena& arena, Module& mod)
: arena(arena)
, mod(mod)
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
    size_t decl_number = 0;
    for (auto& decl : mod.decls) {
        resolveDecl(decl_number++, decl);
    }

    for (auto& decl : mod.decls) {
        checkDecl(decl);
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

bool Checker::mustSubType(const TextSpan &span, Type* sub, Type* super) {
    tctx.flags |= TC_INFER;

    auto conv_result = tctx.SubType(sub, super);
    if (conv_result == TY_CONV_FAIL) {
        fatal(span, "{} is not a subtype of {}", sub->ToString(), super->ToString());
    }

    tctx.flags ^= TC_INFER;

    return conv_result == TY_CONV_CAST;
}

void Checker::mustCast(const TextSpan &span, Type* src, Type* dest) {
    tctx.flags |= TC_INFER;

    if (unsafe_depth > 0) {
        tctx.flags |= TC_UNSAFE;
    } else {
        tctx.flags &= ~TC_UNSAFE;
    }

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

void Checker::finishExpr() {
    tctx.InferAll();
    tctx.Clear();

    bool any_bad_nulls = false;
    for (auto& pair : null_spans) {
        if (pair.first->ty_Untyp.concrete_type == nullptr) {
            error(pair.second, "unable to infer type of null");
            any_bad_nulls = true;
        }
    }

    null_spans.clear();

    if (any_bad_nulls)
        throw CompileError{};
}

/* -------------------------------------------------------------------------- */

void Checker::declareLocal(Symbol* sym) {
    Assert(scope_stack.size() > 0, "declare local on empty scope stack");

    auto& curr_scope = scope_stack.back();

    if (!curr_scope.contains(sym->name)) {
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

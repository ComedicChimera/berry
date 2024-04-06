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
    curr_decl_number = 0;
    for (auto* decl : mod.unsorted_decls) {
        checkDecl(decl);
        curr_decl_number++;
    }

    // Update the declaration numbers of the newly sorted declarations.
    curr_decl_number = 0;
    for (auto* decl : mod.sorted_decls) {
        switch (decl->hir_decl->kind) {
        case HIR_GLOBAL_VAR:
            decl->hir_decl->ir_GlobalVar.symbol->decl_number = curr_decl_number;
            break;
        case HIR_GLOBAL_CONST:
            decl->hir_decl->ir_GlobalConst.symbol->decl_number = curr_decl_number;
            break;
        case HIR_FUNC:
            decl->hir_decl->ir_Func.symbol->decl_number = curr_decl_number;
            break;
        case HIR_STRUCT:
        case HIR_ENUM:
        case HIR_ALIAS:
            decl->hir_decl->ir_TypeDef.symbol->decl_number = curr_decl_number;
            break;
        }
    }

    // Clear out the unsorted declarations list (we don't need it anymore).
    mod.unsorted_decls.clear();
    mod.unsorted_decls.shrink_to_fit();
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

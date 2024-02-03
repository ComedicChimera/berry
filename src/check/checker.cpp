#include "checker.hpp"

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

void Checker::mustNumberType(const TextSpan &span, Type *type) {
    tctx.flags |= TC_INFER;

    if (!tctx.IsNumberType(type)) {
        fatal(span, "expected a number type but got {}", type->ToString());
    }

    tctx.flags ^= TC_INFER;
}

void Checker::mustIntType(const TextSpan &span, Type *type) {
    tctx.flags |= TC_INFER;

    if (!tctx.IsIntType(type)) {
        fatal(span, "expected a integer type but got {}", type->ToString());
    }

    tctx.flags ^= TC_INFER;
}

Untyped* Checker::newUntyped(UntypedKind kind) {
    return arena.New<Untyped>(&tctx, kind);
}

/* -------------------------------------------------------------------------- */

Symbol* Checker::lookup(const std::string &name, const TextSpan &span) {
    for (int i = scope_stack.size() - 1; i >= 0; i--) {
        auto& scope = scope_stack[i];

        auto it = scope.find(name);
        if (it != scope.end()) {
            return it->second;
        }
    }

    auto it = src_file.parent->symbol_table.find(name);
    if (it != src_file.parent->symbol_table.end()) {
        return it->second;
    }

    return nullptr;
}

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

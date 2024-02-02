#include "checker.hpp"

void Checker::MustEqual(const TextSpan &span, Type* a, Type* b) {
    tctx.flags |= TC_INFER;

    if (!tctx.Equal(a, b)) {
        Fatal(span, "type mismatch: {} v. {}", a->ToString(), b->ToString());
    }

    tctx.flags ^= TC_INFER;
}

void Checker::MustSubType(const TextSpan &span, Type* sub, Type* super) {
    tctx.flags |= TC_INFER;

    if (!tctx.SubType(sub, super)) {
        Fatal(span, "{} is not a subtype of {}", sub->ToString(), super->ToString());
    }

    tctx.flags ^= TC_INFER;
}

void Checker::MustCast(const TextSpan &span, Type* src, Type* dest) {
    tctx.flags |= TC_INFER;

    if (!tctx.Cast(src, dest)) {
        Fatal(span, "{} cannot be cast to {}", src->ToString(), dest->ToString());
    }

    tctx.flags ^= TC_INFER;
}

void Checker::MustNumberType(const TextSpan &span, Type *type) {
    tctx.flags |= TC_INFER;

    if (!tctx.IsNumberType(type)) {
        Fatal(span, "expected a number type but got {}", type->ToString());
    }

    tctx.flags ^= TC_INFER;
}

void Checker::MustIntType(const TextSpan &span, Type *type) {
    tctx.flags |= TC_INFER;

    if (!tctx.IsIntType(type)) {
        Fatal(span, "expected a integer type but got {}", type->ToString());
    }

    tctx.flags ^= TC_INFER;
}

Untyped* Checker::NewUntyped(UntypedKind kind) {
    return arena.New<Untyped>(&tctx, kind);
}

/* -------------------------------------------------------------------------- */

Symbol* Checker::Lookup(const std::string &name, const TextSpan &span) {
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

void Checker::DeclareLocal(Symbol* sym) {
    Assert(scope_stack.size() > 0, "declare local on empty scope stack");

    auto& curr_scope = scope_stack.back();

    auto it = curr_scope.find(sym->name);
    if (it == curr_scope.end()) {
        curr_scope[sym->name] = sym;
    } else {
        Fatal(sym->span, "multiple definitions of local variable {} in the same scope", sym->name);
    }
}

void Checker::PushScope() {
    scope_stack.push_back({});
}

void Checker::PopScope() {
    Assert(scope_stack.size() > 0, "pop on empty scope stack");

    scope_stack.pop_back();
}

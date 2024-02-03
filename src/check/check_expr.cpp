#include "ast.hpp"
#include "checker.hpp"

void AstCast::Check(Checker& c) {
    src->Check(c);

    c.MustCast(src->span, src->type, type);
}

void AstBinaryOp::Check(Checker& c) {
    lhs->Check(c);
    rhs->Check(c);

    switch (op_kind) {
    case AOP_ADD: case AOP_SUB: case AOP_MUL: case AOP_DIV: case AOP_MOD:
        c.MustEqual(span, lhs->type, rhs->type);

        c.MustNumberType(lhs->span, lhs->type);

        type = lhs->type;
        break;
    case AOP_BAND: case AOP_BOR: case AOP_BXOR:
        c.MustEqual(span, lhs->type, rhs->type);

        c.MustIntType(lhs->span, lhs->type);

        type = lhs->type;
        break;
    default:
        Panic("unsupported binary operator");
    }
}

void AstUnaryOp::Check(Checker& c) {
    operand->Check(c);

    switch (op_kind) {
    case AOP_NEG:
        c.MustNumberType(operand->span, operand->type);
        type = operand->type;
        break;
    default:
        Panic("unsupported unary operator");
    }
}

void AstAddrOf::Check(Checker& c) {
    elem->Check(c);

    type = c.arena.New<PointerType>(elem->type, is_const);
}

void AstDeref::Check(Checker& c) {
    ptr->Check(c);

    if (ptr->type->Inner()->GetKind() == TYPE_PTR) {
        auto* ptr_type = dynamic_cast<PointerType*>(ptr->type->Inner());

        type = ptr_type->elem_type;
    } else {
        c.Fatal(ptr->span, "expected a pointer type but got {}", ptr->type->ToString());
    }
}

void AstCall::Check(Checker& c) {
    func->Check(c);

    if (func->type->Inner()->GetKind() != TYPE_FUNC) {
        c.Fatal(func->span, "expected a function type but got {}", func->type->ToString());
    }

    auto* func_type = dynamic_cast<FuncType*>(func->type->Inner());

    if (args.size() != func_type->param_types.size()) {
        c.Fatal(func->span, "function expects {} arguments but got {}", func_type->param_types.size(), args.size());
    }

    for (int i = 0; i < args.size(); i++) {
        if (args[i]->Flags() & ASTF_NULL) {
            args[i]->type = func_type->param_types[i];
        } else {
            args[i]->Check(c);
            c.MustSubType(args[i]->span, args[i]->type, func_type->param_types[i]);
        }
    }

    type = func_type->return_type;
}

void AstIdent::Check(Checker& c) {
    symbol = c.Lookup(temp_name, span);
    type = symbol->type;
    temp_name.clear();
}

void AstIntLit::Check(Checker& c) {
    if (type == nullptr) {
        type = c.NewUntyped(UK_NUM);
    }
}

void AstFloatLit::Check(Checker& c) {
    type = c.NewUntyped(UK_FLOAT);
}

void AstBoolLit::Check(Checker& c) {
    // Nothing to do :)
}

void AstNullLit::Check(Checker& c) {
    Panic("unexpected call to check null");
}
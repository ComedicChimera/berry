#include "checker.hpp"

void Checker::Visit(AstCast& node) {
    visitNode(node.src);

    MustCast(node.src->span, node.src->type, node.type);
}

void Checker::Visit(AstBinaryOp& node) {
    visitNode(node.lhs);
    visitNode(node.rhs);

    switch (node.op_kind) {
    case AOP_ADD: case AOP_SUB: case AOP_MUL: case AOP_DIV: case AOP_MOD:
        MustEqual(node.span, node.lhs->type, node.rhs->type);

        MustNumberType(node.lhs->span, node.lhs->type);

        node.type = node.lhs->type;
        break;
    case AOP_BAND: case AOP_BOR: case AOP_BXOR:
        MustEqual(node.span, node.lhs->type, node.rhs->type);

        MustIntType(node.lhs->span, node.lhs->type);

        node.type = node.lhs->type;
        break;
    default:
        Panic("unsupported binary operator");
    }
}

void Checker::Visit(AstUnaryOp& node) {
    visitNode(node.operand);

    switch (node.op_kind) {
    case AOP_NEG:
        MustNumberType(node.operand->span, node.operand->type);
       node.type = node.operand->type;
        break;
    default:
        Panic("unsupported unary operator");
    }
}

void Checker::Visit(AstAddrOf& node) {
    visitNode(node.elem);

    node.type = arena.New<PointerType>(node.elem->type, node.is_const);
}

void Checker::Visit(AstDeref& node) {
    visitNode(node.ptr);

    if (node.ptr->type->Inner()->GetKind() == TYPE_PTR) {
        auto* ptr_type = dynamic_cast<PointerType*>(node.ptr->type->Inner());

        node.type = ptr_type->elem_type;
    } else {
        Fatal(node.ptr->span, "expected a pointer type but got {}", node.ptr->type->ToString());
    }
}

void Checker::Visit(AstCall& node) {
    visitNode(node.func);

    if (node.func->type->Inner()->GetKind() != TYPE_FUNC) {
        Fatal(node.func->span, "expected a function type but got {}", node.func->type->ToString());
    }

    auto* func_type = dynamic_cast<FuncType*>(node.func->type->Inner());

    if (node.args.size() != func_type->param_types.size()) {
        Fatal(node.span, "function expects {} arguments but got {}", func_type->param_types.size(), node.args.size());
    }

    for (int i = 0; i < node.args.size(); i++) {
        if (node.args[i]->Flags() & ASTF_NULL) {
            node.args[i]->type = func_type->param_types[i];
        } else {
            visitNode(node.args[i]);
            MustSubType(node.args[i]->span, node.args[i]->type, func_type->param_types[i]);
        }
    }

   node.type = func_type->return_type;
}

void Checker::Visit(AstIdent& node) {
    node.symbol = Lookup(node.temp_name, node.span);
    node.type = node.symbol->type;
    node.temp_name.clear();
}

void Checker::Visit(AstIntLit& node) {
    if (node.type == nullptr) {
        node.type = NewUntyped(UK_NUM);
    }
}

void Checker::Visit(AstFloatLit& node) {
    node.type = NewUntyped(UK_FLOAT);
}

void Checker::Visit(AstBoolLit& node) {
    // Nothing to do :)
}

void Checker::Visit(AstNullLit& node) {
    Panic("unexpected call to check null");
}
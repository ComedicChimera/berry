#include "checker.hpp"

void Checker::Visit(AstCast& node) {
    visitNode(node.src);

    mustCast(node.src->span, node.src->type, node.type);
}

void Checker::Visit(AstBinaryOp& node) {
    visitNode(node.lhs);
    visitNode(node.rhs);

    node.type = mustApplyBinaryOp(node.span, node.op_kind, node.lhs->type, node.rhs->type);
}

void Checker::Visit(AstUnaryOp& node) {
    visitNode(node.operand);

    node.type = mustApplyUnaryOp(node.span, node.op_kind, node.operand->type);
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
        node.immut = ptr_type->immut;
    } else {
        fatal(node.ptr->span, "expected a pointer type but got {}", node.ptr->type->ToString());
    }
}

void Checker::Visit(AstCall& node) {
    visitNode(node.func);

    if (node.func->type->Inner()->GetKind() != TYPE_FUNC) {
        fatal(node.func->span, "expected a function type but got {}", node.func->type->ToString());
    }

    auto* func_type = dynamic_cast<FuncType*>(node.func->type->Inner());

    if (node.args.size() != func_type->param_types.size()) {
        fatal(node.span, "function expects {} arguments but got {}", func_type->param_types.size(), node.args.size());
    }

    for (int i = 0; i < node.args.size(); i++) {
        if (node.args[i]->GetFlags() & ASTF_NULL) {
            node.args[i]->type = func_type->param_types[i];
        } else {
            visitNode(node.args[i]);
            mustSubType(node.args[i]->span, node.args[i]->type, func_type->param_types[i]);
        }
    }

   node.type = func_type->return_type;
}

void Checker::Visit(AstSlice& node) {
    visitNode(node.array);

    if (node.start_index)
        visitNode(node.start_index);

    if (node.end_index)
        visitNode(node.end_index);

    auto* inner_type = node.array->type->Inner();
    if (inner_type->GetKind() == TYPE_ARRAY) {
        if (node.start_index)
            mustIntType(node.start_index->span, node.start_index->type);

        if (node.end_index)
            mustIntType(node.end_index->span, node.end_index->type);

        node.type = inner_type;

        auto* array_type = dynamic_cast<ArrayType*>(inner_type);
        node.immut = array_type->immut || node.array->immut;
    } else {
        fatal(node.array->span, "{} is not array type", node.array->type->ToString());
    }
}

void Checker::Visit(AstIndex& node) {
    visitNode(node.array);
    visitNode(node.index);

    auto* inner_type = node.array->type->Inner();
    if (inner_type->GetKind() == TYPE_ARRAY) {
        mustIntType(node.index->span, node.index->type);

        auto* array_type = dynamic_cast<ArrayType*>(inner_type);
        node.type = array_type->elem_type;
        node.immut = array_type->immut || node.array->immut;
    } else {
        fatal(node.array->span, "{} is not array type", node.array->type->ToString());
    }
}

void Checker::Visit(AstFieldAccess& node) {
    visitNode(node.root);

    auto root_inner_type = node.root->type->Inner();
    if (root_inner_type->GetKind() == TYPE_ARRAY) {
        if (node.field_name == "_ptr") {
            auto* array_type = dynamic_cast<ArrayType*>(root_inner_type);
            node.type = arena.New<PointerType>(array_type->elem_type);
        } else if (node.field_name == "_len") {
            // TODO: platform sizes?
            node.type = &prim_i64_type;
        } else {
            fatal(node.span, "{} has no field named {}", node.root->type->ToString(), node.field_name);
        }
    } else {
        fatal(node.root->span, "{} is not array type", node.root->type->ToString());
    }
}

void Checker::Visit(AstArrayLit& node) {
    for (auto& elem : node.elements) {
        visitNode(elem);
    }

    auto* first_type = node.elements[0]->type;
    for (int i = 1; i < node.elements.size(); i++) {
        mustEqual(node.elements[i]->span, first_type, node.elements[i]->type);
    }

    node.type = arena.New<ArrayType>(first_type);

    // Move array literal to global memory if necessary.
    if (enclosing_return_type == nullptr) {
        node.alloc_mode = AST_ALLOC_GLOBAL;
    }
}

void Checker::Visit(AstIdent& node) {
    node.symbol = lookup(node.temp_name, node.span);
    node.type = node.symbol->type;
    node.temp_name.clear();
    node.immut = node.symbol->immut;
}

void Checker::Visit(AstIntLit& node) {
    if (node.type == nullptr) {
        node.type = newUntyped(UK_NUM);
    }
}

void Checker::Visit(AstFloatLit& node) {
    node.type = newUntyped(UK_FLOAT);
}

void Checker::Visit(AstBoolLit& node) {
    // Nothing to do :)
}

void Checker::Visit(AstStringLit& node) {
    // Nothing to do :)
}

void Checker::Visit(AstNullLit& node) {
    Panic("unexpected call to check null");
}
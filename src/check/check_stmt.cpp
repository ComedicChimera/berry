#include "checker.hpp"

bool Checker::checkStmt(AstStmt* stmt) {
    return false;
}

/* -------------------------------------------------------------------------- */

void Checker::Visit(AstBlock& node) {
    pushScope();

    for (auto& stmt : node.stmts) {
        visitNode(stmt);

        if (stmt->GetFlags() & ASTF_EXPR) {
            finishExpr();    
        }

        node.always_returns = node.always_returns || stmt->always_returns;
    }

    popScope();
}

/* -------------------------------------------------------------------------- */

void Checker::Visit(AstCondBranch& node) {
    visitNode(node.cond_expr);
    mustEqual(node.cond_expr->span, node.cond_expr->type, &prim_bool_type);

    visitNode(node.body);

    node.always_returns = node.body->always_returns;
}

void Checker::Visit(AstIfTree& node) {
    bool always_returns = true;

    for (auto& branch : node.branches) {
        Visit(branch);

        always_returns = always_returns && branch.always_returns;
    }

    if (node.else_body) {
        visitNode(node.else_body);
    } else {
        always_returns = false;
    }

    node.always_returns = always_returns;
}

void Checker::Visit(AstWhileLoop& node) {
    visitNode(node.cond_expr);
    mustEqual(node.cond_expr->span, node.cond_expr->type, &prim_bool_type);

    loop_depth++;
    visitNode(node.body);
    loop_depth--;

    if (node.else_clause) {
        visitNode(node.else_clause);
    }
}

void Checker::Visit(AstForLoop& node) {
    pushScope();

    if (node.var_def)
        Visit(*node.var_def);

    if (node.cond_expr) {
        visitNode(node.cond_expr);
        mustEqual(node.cond_expr->span, node.cond_expr->type, &prim_bool_type);
    }

    if (node.update_stmt)
        visitNode(node.update_stmt);

    loop_depth++;
    visitNode(node.body);
    loop_depth--;

    if (node.else_clause) {
        visitNode(node.else_clause);
    }

    popScope();
}

/* -------------------------------------------------------------------------- */

void Checker::Visit(AstReturn& node) {
    if (enclosing_return_type == nullptr) {
        error(node.span, "return statement out of enclosing function");
    }

    if (node.value) {
        visitNode(node.value);
        mustSubType(node.value->span, node.value->type, enclosing_return_type);
    } else if (enclosing_return_type->GetKind() != TYPE_UNIT) {
        error(node.span, "enclosing function expects a return value of type {}", enclosing_return_type->ToString());
    }
}

void Checker::Visit(AstBreak& node) {
    if (loop_depth == 0) {
        error(node.span, "break statement occurs outside of loop");
    }
}

void Checker::Visit(AstContinue& node) {
    if (loop_depth == 0) {
        error(node.span, "continue statement occurs outside of loop");
    }
}

/* -------------------------------------------------------------------------- */

void Checker::Visit(AstLocalVarDef& node) {
    if (node.init != nullptr) {
        visitNode(node.init); 

        if (node.symbol->type == nullptr) {
            node.symbol->type = node.init->type;
        } else {
            mustSubType(node.init->span, node.init->type, node.symbol->type);
        }

        finishExpr();
    }

    declareLocal(node.symbol);
}

void Checker::mustBeAssignable(std::unique_ptr<AstExpr>& expr) {
    if (!expr->IsLValue()) {
        error(expr->span, "cannot assign to an r-value");
    }

    if (expr->immut) {
        error(expr->span, "cannot assign to an immutable value");
    }
}

void Checker::Visit(AstAssign& node) {
    visitNode(node.lhs);

    mustBeAssignable(node.lhs);

    visitNode(node.rhs);

    if (node.assign_op_kind == AOP_NONE) {
        mustSubType(node.span, node.rhs->type, node.lhs->type);
    } else {
        Type* result_type = mustApplyBinaryOp(node.span, node.assign_op_kind, node.lhs->type, node.rhs->type);
        mustSubType(node.span, result_type, node.lhs->type);
    }
}

void Checker::Visit(AstIncDec& node) {
    visitNode(node.lhs);

    mustBeAssignable(node.lhs);
    
    Type* result_type = mustApplyBinaryOp(node.span, node.op_kind, node.lhs->type, node.lhs->type);
    mustSubType(node.span, result_type, node.lhs->type);
}

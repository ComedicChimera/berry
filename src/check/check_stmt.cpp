#include "checker.hpp"

bool Checker::checkStmt(AstStmt* node) {
    switch (node->kind) {
    case AST_BLOCK:
        return checkBlock(node);
    case AST_IF:
        return checkIf(node);
    case AST_WHILE:
        checkWhile(node);
        break;
    case AST_FOR:
        checkFor(node);
        break;
    case AST_MATCH:
        return checkMatchStmt(node);
    case AST_LOCAL_VAR:
        checkLocalVar(node);
        break;
    case AST_ASSIGN:
        checkAssign(node);
        break;
    case AST_INCDEC:
        checkIncDec(node);
        break;
    case AST_EXPR_STMT:
        checkExpr(node->an_ExprStmt.expr);
        finishExpr();
        break;
    case AST_RETURN:
        checkReturn(node);
        return true;
    case AST_BREAK:
        if (loop_depth == 0) {
            error(node->span, "break statement outside of loop");
        }
        break;
    case AST_CONTINUE:
        if (loop_depth == 0) {
            error(node->span, "continue statement outside of loop");
        }
        break;
    case AST_FALLTHROUGH:
        if (fallthrough_stack.empty()) {
            error(node->span, "fallthrough statement outside of match");
            return false;
        }

        if (!fallthrough_stack.back()) {
            error(node->span, "cannot fallthrough to case which captures values");
        }

        break;
    default:
        Panic("checking not implemented for stmt {}", (int)node->kind);
    }

    return false;
}

/* -------------------------------------------------------------------------- */

bool Checker::checkBlock(AstStmt* node) {
    pushScope();

    bool always_returns = false;
    for (auto& stmt : node->an_Block.stmts) {
        always_returns = checkStmt(stmt) || always_returns;
    }

    popScope();

    return always_returns;
}

bool Checker::checkIf(AstStmt* node) {
    bool always_returns = true;

    for (auto& branch : node->an_If.branches) {
        pushScope();

        checkExpr(branch.cond_expr, &prim_bool_type);
        mustEqual(branch.cond_expr->span, branch.cond_expr->type, &prim_bool_type);
        finishExpr();

        always_returns = checkStmt(branch.body) && always_returns;

        popScope();
    }

    if (node->an_If.else_block) {
        always_returns = checkStmt(node->an_If.else_block) && always_returns;
    } else {
        always_returns = false;
    }

    return always_returns;
}

void Checker::checkWhile(AstStmt* node) {
    pushScope();
    auto& awhile = node->an_While;

    checkExpr(awhile.cond_expr, &prim_bool_type);
    mustEqual(awhile.cond_expr->span, awhile.cond_expr->type, &prim_bool_type);
    finishExpr();

    loop_depth++;
    checkStmt(awhile.body);
    loop_depth--;

    if (awhile.else_block) {
        checkStmt(awhile.else_block);
    }

    popScope();
}

void Checker::checkFor(AstStmt* node) {
    pushScope();
    auto& afor = node->an_For;

    if (afor.var_def)
        checkStmt(afor.var_def);

    if (afor.cond_expr) {
        checkExpr(afor.cond_expr, &prim_bool_type);
        mustEqual(afor.cond_expr->span, afor.cond_expr->type, &prim_bool_type);
        finishExpr();
    }

    if (afor.update_stmt)
        checkStmt(afor.update_stmt);

    loop_depth++;
    checkStmt(afor.body);
    loop_depth--;

    if (afor.else_block) {
        checkStmt(afor.else_block);
    }

    popScope();
}

bool Checker::checkMatchStmt(AstStmt* node) {
    auto& amatch = node->an_Match;

    checkExpr(amatch.expr);

    std::unordered_set<size_t> enum_usages;
    std::vector<bool> cases_can_fallthrough(amatch.cases.size(), true);
    for (size_t i = 0; i < amatch.cases.size(); i++) {
        auto& acase = amatch.cases[i];

        bool captures = checkCasePattern(acase.cond_expr, amatch.expr->type, &enum_usages);
        if (i > 0 && captures) {
            cases_can_fallthrough[i-1] = false;
        }
    }

    bool all_return = true, hit_always_match = false;
    for (size_t i = 0; i < amatch.cases.size(); i++) {
        auto& acase = amatch.cases[i];

        pushScope();

        declarePatternCaptures(acase.cond_expr);

        fallthrough_stack.push_back(cases_can_fallthrough[i]);
        if (checkStmt(acase.body)) {
            if (PatternAlwaysMatches(acase.cond_expr))
                hit_always_match = true;

            all_return &= true;
        } else if (!hit_always_match) {
            all_return = false;
        }
        fallthrough_stack.pop_back();

        popScope();
    }

    if (hit_always_match)
        return all_return;

    if (all_return) {
        amatch.is_enum_exhaustive = isEnumExhaustive(amatch.expr->type, enum_usages);
        return amatch.is_enum_exhaustive;
    }

    return true;
}

/* -------------------------------------------------------------------------- */

void Checker::checkLocalVar(AstStmt* node) {
    auto& alocal = node->an_LocalVar;

    if (alocal.init_expr != nullptr) {
        is_comptime_expr = true;
        checkExpr(alocal.init_expr, alocal.symbol->type); 

        if (alocal.symbol->flags & SYM_COMPTIME && !is_comptime_expr) {
            error(node->span, "constant initializer must be computable at compile-time");
        }

        if (alocal.symbol->type == nullptr) {
            alocal.symbol->type = alocal.init_expr->type;
        } else {
            mustSubType(alocal.init_expr->span, alocal.init_expr->type, alocal.symbol->type);
        }

        finishExpr();
    }

    declareLocal(alocal.symbol);
}

void Checker::mustBeAssignable(AstExpr* expr) {
    if (!expr->IsLValue() || expr->immut) {
        error(expr->span, "cannot assign to an immutable value");
    }
}

void Checker::checkAssign(AstStmt* node) {
    auto& aassign = node->an_Assign;

    checkExpr(aassign.lhs);

    mustBeAssignable(aassign.lhs);

    checkExpr(aassign.rhs, aassign.lhs->type);

    if (aassign.assign_op == AOP_NONE) {
        mustSubType(node->span, aassign.rhs->type, aassign.lhs->type);
    } else {
        Type* result_type = mustApplyBinaryOp(node->span, aassign.assign_op, aassign.lhs->type, aassign.rhs->type);
        mustSubType(node->span, result_type, aassign.lhs->type);
    }

    finishExpr();
}

void Checker::checkIncDec(AstStmt* node) {
    auto* lhs = node->an_IncDec.lhs;

    checkExpr(lhs);

    mustBeAssignable(lhs);
    
    Type* result_type = mustApplyBinaryOp(node->span, node->an_IncDec.op, lhs->type, lhs->type);
    mustSubType(node->span, result_type, lhs->type);

    finishExpr();
}

/* -------------------------------------------------------------------------- */

void Checker::checkReturn(AstStmt* node) {
    if (enclosing_return_type == nullptr) {
        error(node->span, "return statement out of enclosing function");
    }

    auto* ret_value = node->an_Return.value;
    if (ret_value) {
        checkExpr(ret_value, enclosing_return_type);
        mustSubType(ret_value->span, ret_value->type, enclosing_return_type);
        finishExpr();
    } else if (enclosing_return_type->kind != TYPE_UNIT) {
        error(node->span, "enclosing function expects a return value of type {}", enclosing_return_type->ToString());
    }
}

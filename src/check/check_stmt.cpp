#include "checker.hpp"

std::pair<HirStmt*, bool> Checker::checkStmt(AstNode* node) {
    HirStmt* hstmt;

    switch (node->kind) {
    case AST_BLOCK:
        return checkBlock(node);
    case AST_IF:
        return checkIf(node);
    case AST_WHILE:
        hstmt = checkWhile(node);
        break;
    case AST_DO_WHILE:
        hstmt = checkDoWhile(node);
        break;
    case AST_FOR:
        hstmt = checkFor(node);
        break;
    case AST_MATCH:
        return checkMatchStmt(node);
    case AST_UNSAFE: {
        unsafe_depth++;
        auto [block, always_returns] = checkBlock(node);
        unsafe_depth--;
        return { block, always_returns };
    } break;
    case AST_VAR:
        hstmt = checkLocalVar(node);
        break;
    case AST_CONST:
        hstmt = checkLocalConst(node);
        break;
    case AST_ASSIGN:
        hstmt = checkAssign(node);
        break;
    case AST_INCDEC:
        hstmt = checkIncDec(node);
        break;
    case AST_RETURN:
        return { checkReturn(node), true };
    case AST_BREAK:
        if (loop_depth == 0) {
            error(node->span, "break statement outside of loop");
        }

        hstmt = allocStmt(HIR_BREAK, node->span);
        break;
    case AST_CONTINUE:
        if (loop_depth == 0) {
            error(node->span, "continue statement outside of loop");
        }

        hstmt = allocStmt(HIR_CONTINUE, node->span);
        break;
    case AST_FALLTHRU:
        if (fallthru_stack.empty()) {
            error(node->span, "fallthrough statement outside of match");
        } else if (!fallthru_stack.back()) {
            error(node->span, "cannot fallthrough to case which captures values");
        }

        hstmt = allocStmt(HIR_FALLTHRU, node->span);
        getPatternCtx().fallthru_used = true;
        break;
    default: {
        auto* hexpr = checkExpr(node);
        finishExpr();

        hstmt = allocStmt(HIR_EXPR_STMT, hexpr->span);
        hstmt->ir_ExprStmt.expr = hexpr;
    } break;
    }

    return { hstmt, false };
}

/* -------------------------------------------------------------------------- */

std::pair<HirStmt*, bool> Checker::checkBlock(AstNode* node) {
    pushScope();

    std::vector<HirStmt*> hstmts;
    bool always_returns = false;
    for (auto* astmt : node->an_Block.stmts) {
        auto [hstmt, stmt_always_returns] = checkStmt(astmt);

        hstmts.push_back(hstmt);
        always_returns = stmt_always_returns || always_returns;
    }

    popScope();

    auto* hblock = allocStmt(HIR_BLOCK, node->span);
    hblock->ir_Block.stmts = arena.MoveVec(std::move(hstmts));
    return { hblock, always_returns };
}

std::pair<HirStmt*, bool> Checker::checkIf(AstNode* node) {
    std::vector<HirIfBranch> hbranches;
    bool always_returns = true;

    for (auto& abranch : node->an_If.branches) {
        pushScope();

        auto* hcond = checkExpr(abranch.cond, &prim_bool_type);
        mustEqual(hcond->span, hcond->type, &prim_bool_type);
        finishExpr();

        auto [hbody, body_always_returns] = checkStmt(abranch.body);
        always_returns = body_always_returns && always_returns;

        hbranches.emplace_back(hcond, hbody);

        popScope();
    }

    HirStmt* helse_stmt = nullptr;
    if (node->an_If.else_stmt) {
        auto [hbody, else_always_returns] = checkStmt(node->an_If.else_stmt);

        helse_stmt = hbody;
        always_returns = else_always_returns && always_returns;
    } else {
        always_returns = false;
    }

    auto* hif = allocStmt(HIR_IF, node->span);
    hif->ir_If.branches = arena.MoveVec(std::move(hbranches));
    hif->ir_If.else_stmt = helse_stmt;

    return { hif, always_returns };
}

HirStmt* Checker::checkWhile(AstNode* node) {
    pushScope();
    auto& awhile = node->an_While;

    auto* hcond = checkExpr(awhile.cond, &prim_bool_type);
    mustEqual(hcond->span, hcond->type, &prim_bool_type);
    finishExpr();

    loop_depth++;
    auto* hbody = checkStmt(awhile.body).first;
    loop_depth--;

    HirStmt* helse_stmt = nullptr;
    if (awhile.else_stmt) {
        helse_stmt = checkStmt(awhile.else_stmt).first;
    }

    popScope();

    auto* hwhile = allocStmt(HIR_WHILE, node->span);
    hwhile->ir_While.cond = hcond;
    hwhile->ir_While.body = hbody;
    hwhile->ir_While.else_stmt = helse_stmt;
    return hwhile;
}

HirStmt* Checker::checkDoWhile(AstNode* node) {
    auto& awhile = node->an_While;

    loop_depth++;
    auto* hbody = checkStmt(awhile.body).first;
    loop_depth--;

    pushScope();
    auto* hcond = checkExpr(awhile.cond, &prim_bool_type);
    mustEqual(hcond->span, hcond->type, &prim_bool_type);
    finishExpr();
    popScope();

    HirStmt* helse_stmt = nullptr;
    if (awhile.else_stmt) {
        helse_stmt = checkStmt(awhile.else_stmt).first;
    }

    auto* hwhile = allocStmt(HIR_DO_WHILE, node->span);
    hwhile->ir_While.cond = hcond;
    hwhile->ir_While.body = hbody;
    hwhile->ir_While.else_stmt = helse_stmt;
    return hwhile;
}

HirStmt* Checker::checkFor(AstNode* node) {
    pushScope();
    auto& afor = node->an_For;

    HirStmt* hiter_var = nullptr;
    if (afor.iter_var)
        hiter_var = checkStmt(afor.iter_var).first;

    HirExpr* hcond = nullptr;
    if (afor.cond) {
        hcond = checkExpr(afor.cond, &prim_bool_type);
        mustEqual(hcond->span, hcond->type, &prim_bool_type);
        finishExpr();
    }

    HirStmt* hupdate = nullptr;
    if (afor.update_stmt)
        hupdate = checkStmt(afor.update_stmt).first;

    loop_depth++;
    auto* hbody = checkStmt(afor.body).first;
    loop_depth--;

    HirStmt* helse_stmt = nullptr;
    if (afor.else_stmt) {
        helse_stmt = checkStmt(afor.else_stmt).first;
    }

    popScope();

    auto* hfor = allocStmt(HIR_FOR, node->span);
    hfor->ir_For.iter_var = hiter_var;
    hfor->ir_For.cond = hcond;
    hfor->ir_For.update_stmt = hupdate;
    hfor->ir_For.body = hbody;
    hfor->ir_For.else_stmt = helse_stmt;
    return hfor;
}


static bool patternAlwaysMatches(AstNode* pattern) {
    switch (pattern->kind) {
    case AST_EXPR_LIST:
        for (auto* sub_pattern : pattern->an_ExprList.exprs) {
            if (patternAlwaysMatches(sub_pattern))
                return true;
        }
        break;
    case AST_IDENT:
        return true;
    }

    return false;
}

std::pair<HirStmt*, bool> Checker::checkMatchStmt(AstNode* node) {
    auto& amatch = node->an_Match;
    auto* hcond = checkExpr(amatch.expr);
    finishExpr();

    pushPatternCtx();

    std::vector<HirCaseBlock> hcases;
    std::vector<bool> cases_can_fallthrough(amatch.cases.size(), true);
    for (size_t i = 0; i < amatch.cases.size(); i++) {
        auto& acase = amatch.cases[i];

        auto [hpatterns, captures] = checkCasePattern(acase.cond, hcond->type);
        if (i > 0 && captures) {
            cases_can_fallthrough[i-1] = false;
        }

        hcases.emplace_back(hpatterns);
    }

    bool all_return = true, hit_always_match = false;
    for (size_t i = 0; i < amatch.cases.size(); i++) {
        auto& hcase = hcases[i];

        pushScope();

        declarePatternCaptures(hcase.patterns[0]);

        fallthru_stack.push_back(cases_can_fallthrough[i]);
        auto [hbody, case_always_returns] = checkStmt(amatch.cases[i].body);
        if (case_always_returns) {
            if (patternAlwaysMatches(amatch.cases[i].cond)) {
                hit_always_match = true;
            }

            all_return &= !getPatternCtx().fallthru_used;
        } else {
            all_return = false;
        }
        fallthru_stack.pop_back();

        popScope();

        hcase.body = hbody;
    }

    auto* hmatch = allocStmt(HIR_MATCH, node->span);
    hmatch->ir_Match.expr = hcond;
    hmatch->ir_Match.cases = arena.MoveVec(std::move(hcases));
    hmatch->ir_Match.is_implicit_exhaustive = false;

    if (hit_always_match) {
        popPatternCtx();
        return {hmatch, all_return};
    } else if (all_return) {
        hmatch->ir_Match.is_implicit_exhaustive = isEnumExhaustive(hcond->type);
        popPatternCtx();
        return {hmatch, hmatch->ir_Match.is_implicit_exhaustive};
    }

    return {hmatch, false};
}

/* -------------------------------------------------------------------------- */

HirStmt* Checker::checkLocalVar(AstNode* node) {
    auto& alocal = node->an_Var;

    Type* type = nullptr;
    if (alocal.type) {
        type = checkTypeLabel(alocal.type, false);
    }

    HirExpr* hinit = nullptr;
    if (alocal.init) {
        hinit = checkExpr(alocal.init, type); 

        if (type) {
            hinit = subtypeCast(hinit, type);
        } else {
            type = hinit->type;
        }

        finishExpr();
    }

    alocal.symbol->type = type;
    declareLocal(alocal.symbol);

    auto* hvar = allocStmt(HIR_LOCAL_VAR, node->span);
    hvar->ir_LocalVar.symbol = alocal.symbol;
    hvar->ir_LocalVar.init = hinit;

    // Local variables are always stack-allocated by default.
    hvar->ir_LocalVar.alloc_mode = HIRMEM_STACK;

    // May be promoted to roots by escape analyzer.
    hvar->ir_LocalVar.is_gcroot = false;
    return hvar;
}

HirStmt* Checker::checkLocalConst(AstNode* node) {
    auto& alocal = node->an_Var;

    Type* type = nullptr;
    if (alocal.type) {
        type = checkTypeLabel(alocal.type, false);
    }

    ConstValue* value;
    if (alocal.init) {
        comptime_depth++;
        auto* hinit = checkExpr(alocal.init, type); 
        comptime_depth--;

        if (type) {
            hinit = subtypeCast(hinit, type);
        } else {
            type = hinit->type;
        }

        finishExpr();

        value = evalComptime(hinit);
    } else {
        value = getComptimeNull(type);
    }

    alocal.symbol->type = type;
    declareLocal(alocal.symbol);

    auto* hconst = allocStmt(HIR_LOCAL_CONST, node->span);
    hconst->ir_LocalConst.symbol = alocal.symbol;
    hconst->ir_LocalConst.init = value;
    return hconst;
}

static std::unordered_map<TokenKind, HirOpKind> assign_ops {
    { TOK_PLUS_ASSIGN, HIROP_ADD },
    { TOK_MINUS_ASSIGN, HIROP_SUB },
    { TOK_STAR_ASSIGN, HIROP_MUL },
    { TOK_FSLASH_ASSIGN, HIROP_DIV },
    { TOK_MOD_ASSIGN, HIROP_MOD },
    { TOK_SHL_ASSIGN, HIROP_SHL },
    { TOK_SHR_ASSIGN, HIROP_SHR },
    { TOK_AMP_ASSIGN, HIROP_BWAND },
    { TOK_PIPE_ASSIGN, HIROP_BWOR },
    { TOK_CARRET_ASSIGN, HIROP_BWXOR },
    { TOK_AND_ASSIGN, HIROP_LGAND },
    { TOK_OR_ASSIGN, HIROP_LGOR }
};

HirStmt* Checker::checkAssign(AstNode* node) {
    auto& aassign = node->an_Assign;

    auto* hlhs = checkExpr(aassign.lhs);
    if (!hlhs->assignable) {
        fatal(hlhs->span, "value is not assignable");
    }

    if (aassign.op.tok_kind == TOK_ASSIGN) {
        auto* hrhs = checkExpr(aassign.rhs, hlhs->type);
        hrhs = subtypeCast(hrhs, hlhs->type);
        finishExpr();

        auto* hassign = allocStmt(HIR_ASSIGN, node->span);
        hassign->ir_Assign.lhs = hlhs;
        hassign->ir_Assign.rhs = hrhs;
        return hassign;
    }

    auto* hrhs = checkExpr(aassign.rhs);
    auto op = assign_ops[aassign.op.tok_kind];
    auto* result_type = mustApplyBinaryOp(node->span, op, hlhs->type, hrhs->type);
    bool needs_subtype_cast = mustSubType(node->span, result_type, hlhs->type);
    finishExpr();

    auto* hassign = allocStmt(HIR_CPD_ASSIGN, node->span);
    hassign->ir_CpdAssign.lhs = hlhs;
    hassign->ir_CpdAssign.rhs = hrhs;
    hassign->ir_CpdAssign.op = op;
    hassign->ir_CpdAssign.binop_type = result_type;
    hassign->ir_CpdAssign.needs_subtype_cast = needs_subtype_cast;
    return hassign;
}

HirStmt* Checker::checkIncDec(AstNode* node) {
    auto* hlhs = checkExpr(node->an_IncDec.lhs);
    if (!hlhs->assignable) {
        fatal(hlhs->span, "value is not assignable");
    }

    auto op = node->an_IncDec.op.tok_kind == TOK_INC ? HIROP_ADD : HIROP_SUB;

    auto* rhs_type = hlhs->type;
    if (rhs_type->Inner()->kind == TYPE_PTR) { // Pointer Arithmetic
        rhs_type = platform_int_type;
    }
    
    auto* result_type = mustApplyBinaryOp(node->span, op, hlhs->type, rhs_type);
    bool needs_subtype_cast = mustSubType(node->span, result_type, hlhs->type);
    finishExpr();

    auto* hinc_dec = allocStmt(HIR_INCDEC, node->span);
    hinc_dec->ir_IncDec.expr = hlhs;
    hinc_dec->ir_IncDec.op = op;

    hinc_dec->ir_IncDec.binop_type = result_type;
    hinc_dec->ir_IncDec.needs_subtype_cast = needs_subtype_cast;
    return hinc_dec;
}

/* -------------------------------------------------------------------------- */

HirStmt* Checker::checkReturn(AstNode* node) {
    if (enclosing_return_type == nullptr) {
        error(node->span, "return statement out of enclosing function");
    }

    HirExpr* hexpr = nullptr;
    if (node->an_Return.expr) {
        hexpr = checkExpr(node->an_Return.expr, enclosing_return_type);
        hexpr = subtypeCast(hexpr, enclosing_return_type);
        finishExpr();
    } else if (enclosing_return_type->kind != TYPE_UNIT) {
        error(node->span, "enclosing function expects a return value of type {}", enclosing_return_type->ToString());
    }

    auto* hret = allocStmt(HIR_RETURN, node->span);
    hret->ir_Return.expr = hexpr;
    return hret;
}

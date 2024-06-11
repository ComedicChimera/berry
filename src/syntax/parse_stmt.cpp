#include "parser.hpp"

AstNode* Parser::parseBlock() {
    auto start_span = tok.span;
    want(TOK_LBRACE);

    std::vector<AstNode*> stmts;
    while (!has(TOK_RBRACE)) {
        stmts.emplace_back(parseStmt());
    }


    auto end_span = tok.span;
    want(TOK_RBRACE);

    AstNode* block = allocNode(AST_BLOCK, SpanOver(start_span, end_span));
    block->an_Block.stmts = ast_arena.MoveVec(std::move(stmts));
    return block;
}

AstNode* Parser::parseStmt() {
    AstNode* stmt;

    switch (tok.kind) {
    case TOK_LET:
    case TOK_CONST:
        stmt = parseLocalVarDecl();
        break;
    case TOK_BREAK:
        next();
        stmt = allocNode(AST_BREAK, prev.span);
        break;
    case TOK_CONTINUE:
        next();
        stmt = allocNode(AST_CONTINUE, prev.span);
        break;
    case TOK_FALLTHROUGH:
        next();
        stmt = allocNode(AST_FALLTHRU, prev.span);
        break;
    case TOK_RETURN: {
        next();

        if (!has(TOK_SEMI)) {
            auto start_span = prev.span;
            auto expr = parseExpr();

            auto span = SpanOver(start_span, expr->span);
            stmt = allocNode(AST_RETURN, span);
            stmt->an_Return.expr = expr;
        } else {
            stmt = allocNode(AST_RETURN, prev.span);
            stmt->an_Return.expr = nullptr;
        }
    } break;
    case TOK_IF:
        return parseIfStmt();
    case TOK_WHILE:
        return parseWhileLoop();
    case TOK_DO:
        return parseDoWhileLoop();
    case TOK_FOR:
        return parseForLoop();
    case TOK_MATCH:
        return parseMatchStmt();
    case TOK_UNSAFE: {
        next();
        auto start_span = prev.span;

        auto* block = parseBlock();
        block->kind = AST_UNSAFE;
        block->span.start_line = start_span.start_line;
        block->span.start_col = start_span.start_col;

        return block;
    } break;
    default:
        stmt = parseExprAssignStmt();
    }

    want(TOK_SEMI);
    return stmt;
}

/* -------------------------------------------------------------------------- */

AstNode* Parser::parseIfStmt() {
    std::vector<AstCondBranch> branches;

    while (true) {
        next();
        auto start_span = prev.span;

        pushAllowStructLit(false);
        auto cond_expr = parseExpr();
        popAllowStructLit();
        
        auto body = parseBlock();

        auto span = SpanOver(start_span, body->span);
        branches.emplace_back(
            span,
            cond_expr,
            body
        );

        if (!has(TOK_ELIF)) {
            break;
        }
    }

    auto else_stmt = maybeParseElse();
    auto span = SpanOver(branches[0].span, else_stmt != nullptr ? else_stmt->span : branches.back().span);
    auto* aif = allocNode(AST_IF, span);
    aif->an_If.branches = ast_arena.MoveVec(std::move(branches));
    aif->an_If.else_stmt = else_stmt;
    return aif;
}

AstNode* Parser::parseWhileLoop() {
    next();
    auto start_span = prev.span;

    pushAllowStructLit(false);
    auto cond_expr = parseExpr();
    popAllowStructLit();

    auto body = parseBlock();
    auto else_stmt = maybeParseElse();

    auto span = SpanOver(start_span, else_stmt ? else_stmt->span : body->span);
    auto* awhile = allocNode(AST_WHILE, span);
    awhile->an_While.cond = cond_expr;
    awhile->an_While.body = body;
    awhile->an_While.else_stmt = else_stmt;
    return awhile;
}

AstNode* Parser::parseDoWhileLoop() {
    next();
    auto start_span = prev.span;

    auto body = parseBlock();

    want(TOK_WHILE);
    auto cond_expr = parseExpr();

    AstNode* else_stmt { nullptr };
    TextSpan end_span;
    if (has(TOK_ELSE)) {
        next();

        else_stmt = parseBlock();
        end_span = else_stmt->span;
    } else {
        want(TOK_SEMI);
        end_span = prev.span;
    }

    auto* awhile = allocNode(AST_DO_WHILE, SpanOver(start_span, end_span));
    awhile->an_While.cond = cond_expr;
    awhile->an_While.body = body;
    awhile->an_While.else_stmt = else_stmt;
    return awhile;
}  

AstNode* Parser::parseForLoop() {
    next();
    auto start_span = prev.span;

    AstNode* iter_var { nullptr };
    if (has(TOK_LET)) {
        iter_var = parseLocalVarDecl();
    }

    want(TOK_SEMI);

    AstNode* cond_expr { nullptr }; 
    if (!has(TOK_SEMI)) {
        cond_expr = parseExpr();
    }

    want(TOK_SEMI);

    AstNode* update_stmt { nullptr };
    if (!has(TOK_LBRACE)) {
        pushAllowStructLit(false);
        update_stmt = parseExprAssignStmt();
        popAllowStructLit();
    }

    auto body = parseBlock();
    auto else_stmt = maybeParseElse();

    auto span = SpanOver(start_span, else_stmt ? else_stmt->span : body->span);
    auto* afor = allocNode(AST_FOR, span);
    afor->an_For.iter_var = iter_var;
    afor->an_For.cond = cond_expr;
    afor->an_For.update_stmt = update_stmt;
    afor->an_For.body = body;
    afor->an_For.else_stmt = else_stmt;
    return afor;
}

AstNode* Parser::maybeParseElse() {
    if (has(TOK_ELSE)) {
        next();

        return parseBlock();
    }

    return nullptr;
}

AstNode* Parser::parseMatchStmt() {
    auto start_span = tok.span;
    next();

    pushAllowStructLit(false);
    auto* expr = parseExpr();
    popAllowStructLit();

    want(TOK_LBRACE);

    std::vector<AstCondBranch> cases;
    while (has(TOK_CASE)) {
        auto case_start_span = tok.span;
        next();

        auto* pattern = parseCasePattern();

        want(TOK_COLON);

        std::vector<AstNode*> stmts;
        while (!has(TOK_CASE) && !has(TOK_RBRACE)) {
            stmts.push_back(parseStmt());
        }

        AstNode* case_block;
        if (stmts.size() > 0) {
            case_block = allocNode(AST_BLOCK, SpanOver(stmts[0]->span, stmts.back()->span));
            case_block->an_Block.stmts = ast_arena.MoveVec(std::move(stmts));
        }
        
        cases.emplace_back(SpanOver(case_start_span, prev.span), pattern, case_block);
    }

    want(TOK_RBRACE);

    auto* amatch = allocNode(AST_MATCH, SpanOver(start_span, prev.span));
    amatch->an_Match.expr = expr;
    amatch->an_Match.cases = ast_arena.MoveVec(std::move(cases));
    return amatch;
}

/* -------------------------------------------------------------------------- */

AstNode* Parser::parseLocalVarDecl() {
    auto start_span = tok.span;
    bool comptime = false;
    if (has(TOK_CONST)) {
        next();
        comptime = true;
    } else {
        want(TOK_LET);
    }

    auto name_tok = wantAndGet(TOK_IDENT);

    AstNode* type = nullptr;
    AstNode* init_expr { nullptr };
    TextSpan end_span;
    if (has(TOK_COLON)) {
        type = parseTypeExt();

        if (has(TOK_ASSIGN)) {
            init_expr = parseInitializer();
            end_span = init_expr->span;
        } else {
            end_span = prev.span;
        }
    } else {
        init_expr = parseInitializer();
        end_span = init_expr->span;
    }

    Symbol* symbol = global_arena.New<Symbol>(
        src_file.parent->id,
        global_arena.MoveStr(std::move(name_tok.value)),
        name_tok.span,
        comptime ? SYM_CONST : SYM_VAR,
        0,
        nullptr,
        comptime
    );

    auto* alocal = allocNode(comptime ? AST_CONST : AST_VAR, SpanOver(start_span, end_span));
    alocal->an_Var.symbol = symbol;
    alocal->an_Var.type = type;
    alocal->an_Var.init = init_expr;
    return alocal;
}

AstNode* Parser::parseExprAssignStmt() {
    auto lhs = parseExpr();

    AstOper assign_op { tok.span, tok.kind };
    switch (tok.kind) {
    case TOK_ASSIGN:
    case TOK_PLUS_ASSIGN:
    case TOK_MINUS_ASSIGN:
    case TOK_STAR_ASSIGN:
    case TOK_FSLASH_ASSIGN:
    case TOK_MOD_ASSIGN:
    case TOK_SHL_ASSIGN:
    case TOK_SHR_ASSIGN:
    case TOK_AMP_ASSIGN:
    case TOK_PIPE_ASSIGN:
    case TOK_CARRET_ASSIGN:
    case TOK_AND_ASSIGN:
    case TOK_OR_ASSIGN:
        next();
        break;
    case TOK_INC: {
        next();
        auto span = SpanOver(lhs->span, prev.span);

        auto* aid = allocNode(AST_INCDEC, span);
        aid->an_IncDec.lhs = lhs;
        aid->an_IncDec.op = assign_op;
        return aid;
    } break;    
    case TOK_DEC:{
        next();
        auto span = SpanOver(lhs->span, prev.span);

        auto* aid = allocNode(AST_INCDEC, span);
        aid->an_IncDec.lhs = lhs;
        aid->an_IncDec.op = assign_op;
        return aid;
    } break;
    default: 
        return lhs;
    }

    auto rhs = parseExpr();

    auto span = SpanOver(lhs->span, rhs->span);
    auto* aassign = allocNode(AST_ASSIGN, span);
    aassign->an_Assign.lhs = lhs;
    aassign->an_Assign.rhs = rhs;
    aassign->an_Assign.op = assign_op;
    return aassign;
}
#include "parser.hpp"

AstStmt* Parser::parseBlock() {
    auto start_span = tok.span;
    want(TOK_LBRACE);

    std::vector<AstStmt*> stmts;
    while (!has(TOK_RBRACE)) {
        stmts.emplace_back(parseStmt());
    }


    auto end_span = tok.span;
    want(TOK_RBRACE);

    AstStmt* block = allocStmt(AST_BLOCK, SpanOver(start_span, end_span));
    block->an_Block.stmts = arena.MoveVec(std::move(stmts));
    return block;
}

AstStmt* Parser::parseStmt() {
    AstStmt* stmt;

    switch (tok.kind) {
    case TOK_LET:
    case TOK_CONST:
        stmt = parseLocalVarDef();
        break;
    case TOK_BREAK:
        next();
        stmt = allocStmt(AST_BREAK, prev.span);
        break;
    case TOK_CONTINUE:
        next();
        stmt = allocStmt(AST_CONTINUE, prev.span);
        break;
    case TOK_RETURN: {
        next();

        if (!has(TOK_SEMI)) {
            auto start_span = prev.span;
            auto expr = parseExpr();

            auto span = SpanOver(start_span, expr->span);
            stmt = allocStmt(AST_RETURN, span);
            stmt->an_Return.value = expr;
        } else {
            stmt = allocStmt(AST_RETURN, prev.span);
            stmt->an_Return.value = nullptr;
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
    default:
        stmt = parseExprAssignStmt();
    }

    want(TOK_SEMI);
    return stmt;
}

/* -------------------------------------------------------------------------- */

AstStmt* Parser::parseIfStmt() {
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

    auto else_block = maybeParseElse();
    auto span = SpanOver(branches[0].span, else_block != nullptr ? else_block->span : branches.back().span);
    auto* aif = allocStmt(AST_IF, span);
    aif->an_If.branches = arena.MoveVec(std::move(branches));
    aif->an_If.else_block = else_block;
    return aif;
}

AstStmt* Parser::parseWhileLoop() {
    next();
    auto start_span = prev.span;

    pushAllowStructLit(false);
    auto cond_expr = parseExpr();
    popAllowStructLit();

    auto body = parseBlock();
    auto else_block = maybeParseElse();

    auto span = SpanOver(start_span, else_block ? else_block->span : body->span);
    auto* awhile = allocStmt(AST_WHILE, span);
    awhile->an_While.cond_expr = cond_expr;
    awhile->an_While.body = body;
    awhile->an_While.else_block = else_block;
    awhile->an_While.is_do_while = false;
    return awhile;
}

AstStmt* Parser::parseDoWhileLoop() {
    next();
    auto start_span = prev.span;

    auto body = parseBlock();

    want(TOK_WHILE);
    auto cond_expr = parseExpr();

    AstStmt* else_block { nullptr };
    TextSpan end_span;
    if (has(TOK_ELSE)) {
        next();

        else_block = parseBlock();
        end_span = else_block->span;
    } else {
        want(TOK_SEMI);
        end_span = prev.span;
    }

    auto* awhile = allocStmt(AST_WHILE, SpanOver(start_span, end_span));
    awhile->an_While.cond_expr = cond_expr;
    awhile->an_While.body = body;
    awhile->an_While.else_block = else_block;
    awhile->an_While.is_do_while = true;
    return awhile;
}  

AstStmt* Parser::parseForLoop() {
    next();
    auto start_span = prev.span;

    AstStmt* var_def { nullptr };
    if (has(TOK_LET)) {
        var_def = parseLocalVarDef();
    }

    want(TOK_SEMI);

    AstExpr* cond_expr { nullptr }; 
    if (!has(TOK_SEMI)) {
        cond_expr = parseExpr();
    }

    want(TOK_SEMI);

    AstStmt* update_stmt { nullptr };
    if (!has(TOK_LBRACE)) {
        pushAllowStructLit(false);
        update_stmt = parseExprAssignStmt();
        popAllowStructLit();
    }

    auto body = parseBlock();
    auto else_block = maybeParseElse();

    auto span = SpanOver(start_span, else_block ? else_block->span : body->span);
    auto* afor = allocStmt(AST_FOR, span);
    afor->an_For.var_def = var_def;
    afor->an_For.cond_expr = cond_expr;
    afor->an_For.update_stmt = update_stmt;
    afor->an_For.body = body;
    afor->an_For.else_block = else_block;
    return afor;
}

AstStmt* Parser::maybeParseElse() {
    if (has(TOK_ELSE)) {
        next();

        return parseBlock();
    }

    return nullptr;
}

/* -------------------------------------------------------------------------- */

AstStmt* Parser::parseLocalVarDef() {
    auto start_span = tok.span;
    bool comptime = tok.kind == TOK_CONST;
    want(TOK_LET);

    auto name_tok = wantAndGet(TOK_IDENT);

    Type* type = nullptr;
    AstExpr* init { nullptr };
    TextSpan end_span;
    if (has(TOK_COLON)) {
        type = parseTypeExt();

        if (has(TOK_ASSIGN)) {
            init = parseInitializer();
            end_span = init->span;
        } else {
            end_span = prev.span;
        }
    } else {
        init = parseInitializer();
        end_span = init->span;
    }

    Symbol* symbol = arena.New<Symbol>(
        src_file.parent->id,
        arena.MoveStr(std::move(name_tok.value)),
        name_tok.span,
        comptime ? SYM_CONST : SYM_VAR,
        0,
        type,
        comptime
    );

    auto* alocal = allocStmt(AST_LOCAL_VAR, SpanOver(start_span, end_span));
    alocal->an_LocalVar.symbol = symbol;
    alocal->an_LocalVar.init = init;
    return alocal;
}

AstStmt* Parser::parseExprAssignStmt() {
    auto lhs = parseExpr();

    AstOpKind assign_op = AOP_NONE;
    switch (tok.kind) {
    case TOK_ASSIGN:
        next();
        break;
    case TOK_PLUS_ASSIGN:
        next();
        assign_op = AOP_ADD;
        break;
    case TOK_MINUS_ASSIGN:
        next();
        assign_op = AOP_SUB;
        break;
    case TOK_STAR_ASSIGN:
        next();
        assign_op = AOP_MUL;
        break;
    case TOK_FSLASH_ASSIGN:
        next();
        assign_op = AOP_DIV;
        break;
    case TOK_MOD_ASSIGN:
        next();
        assign_op = AOP_MOD;
        break;
    case TOK_SHL_ASSIGN:
        next();
        assign_op = AOP_SHL;
        break;
    case TOK_SHR_ASSIGN:
        next();
        assign_op = AOP_SHR;
        break;
    case TOK_AMP_ASSIGN:
        next();
        assign_op = AOP_BWAND;
        break;
    case TOK_PIPE_ASSIGN:
        next();
        assign_op = AOP_BWOR;
        break;
    case TOK_CARRET_ASSIGN:
        next();
        assign_op = AOP_BWXOR;
        break;
    case TOK_AND_ASSIGN:
        next();
        assign_op = AOP_LGAND;
        break;
    case TOK_OR_ASSIGN:
        next();
        assign_op = AOP_LGOR;
        break;
    case TOK_INC: {
        next();
        auto span = SpanOver(lhs->span, prev.span);

        auto* aid = allocStmt(AST_INCDEC, span);
        aid->an_IncDec.lhs = lhs;
        aid->an_IncDec.op = AOP_ADD;
        return aid;
    } break;    
    case TOK_DEC:{
        next();
        auto span = SpanOver(lhs->span, prev.span);

        auto* aid = allocStmt(AST_INCDEC, span);
        aid->an_IncDec.lhs = lhs;
        aid->an_IncDec.op = AOP_SUB;
        return aid;
    } break;
    default: {
        auto* expr_stmt = allocStmt(AST_EXPR_STMT, lhs->span);
        expr_stmt->an_ExprStmt.expr = lhs;
        return expr_stmt;
    } break;
    }

    auto rhs = parseExpr();

    auto span = SpanOver(lhs->span, rhs->span);
    auto* aassign = allocStmt(AST_ASSIGN, span);
    aassign->an_Assign.lhs = lhs;
    aassign->an_Assign.rhs = rhs;
    aassign->an_Assign.assign_op = assign_op;
    return aassign;
}
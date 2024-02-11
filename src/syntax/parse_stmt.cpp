#include "parser.hpp"

std::unique_ptr<AstNode> Parser::parseBlock() {
    auto start_span = tok.span;
    want(TOK_LBRACE);

    std::vector<std::unique_ptr<AstNode>> stmts;
    while (!has(TOK_RBRACE)) {
        stmts.emplace_back(parseStmt());
    }


    auto end_span = tok.span;
    want(TOK_RBRACE);
    return std::make_unique<AstBlock>(SpanOver(start_span, end_span), std::move(stmts));
}

std::unique_ptr<AstNode> Parser::parseStmt() {
    std::unique_ptr<AstNode> stmt;

    switch (tok.kind) {
    case TOK_LET:
        stmt = parseLocalVarDef();
        break;
    case TOK_BREAK:
        next();
        stmt = std::make_unique<AstBreak>(prev.span);
        break;
    case TOK_CONTINUE:
        next();
        stmt = std::make_unique<AstContinue>(prev.span);
        break;
    case TOK_RETURN:
        next();

        if (!has(TOK_SEMI)) {
            auto start_span = prev.span;
            auto expr = parseExpr();

            auto span = SpanOver(start_span, expr->span);
            stmt = std::make_unique<AstReturn>(span, std::move(expr));
        } else {
            stmt = std::make_unique<AstReturn>(prev.span);
        }

        break;
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

std::unique_ptr<AstNode> Parser::parseIfStmt() {
    std::vector<AstCondBranch> branches;

    while (true) {
        next();
        auto start_span = prev.span;

        auto cond_expr = parseExpr();
        auto body = parseBlock();

        auto span = SpanOver(start_span, body->span);
        branches.emplace_back(
            span,
            std::move(cond_expr),
            std::move(body)
        );

        if (!has(TOK_ELIF)) {
            break;
        }
    }

    auto else_block = maybeParseElse();
    return std::make_unique<AstIfTree>(std::move(branches), std::move(else_block));
}

std::unique_ptr<AstNode> Parser::parseWhileLoop() {
    next();
    auto start_span = prev.span;

    auto cond_expr = parseExpr();
    auto body = parseBlock();
    auto else_block = maybeParseElse();

    auto span = SpanOver(start_span, else_block ? else_block->span : body->span);
    return std::make_unique<AstWhileLoop>(
        span, 
        std::move(cond_expr), 
        std::move(body), 
        std::move(else_block), 
        false
    );
}

std::unique_ptr<AstNode> Parser::parseDoWhileLoop() {
    next();
    auto start_span = prev.span;

    auto body = parseBlock();

    want(TOK_WHILE);
    auto cond_expr = parseExpr();

    std::unique_ptr<AstNode> else_block;
    TextSpan end_span;
    if (has(TOK_ELSE)) {
        next();

        else_block = parseBlock();
        end_span = else_block->span;
    } else {
        want(TOK_SEMI);
        end_span = prev.span;
    }

    return std::make_unique<AstWhileLoop>(
        SpanOver(start_span, end_span), 
        std::move(cond_expr), 
        std::move(body), 
        std::move(else_block), 
        true
    );
}  

std::unique_ptr<AstNode> Parser::parseForLoop() {
    next();
    auto start_span = prev.span;

    std::unique_ptr<AstLocalVarDef> var_def { nullptr };
    if (has(TOK_LET)) {
        var_def = parseLocalVarDef();
    }

    want(TOK_SEMI);

    std::unique_ptr<AstExpr> cond_expr { nullptr }; 
    if (!has(TOK_SEMI)) {
        cond_expr = parseExpr();
    }

    want(TOK_SEMI);

    std::unique_ptr<AstNode> update_stmt { nullptr };
    if (!has(TOK_LBRACE)) {
        update_stmt = parseExprAssignStmt();
    }

    auto body = parseBlock();
    auto else_block = maybeParseElse();

    auto span = SpanOver(start_span, else_block ? else_block->span : body->span);
    return std::make_unique<AstForLoop>(
        span,
        std::move(var_def),
        std::move(cond_expr),
        std::move(update_stmt),
        std::move(body),
        std::move(else_block)
    );
}

std::unique_ptr<AstNode> Parser::maybeParseElse() {
    if (has(TOK_ELSE)) {
        next();

        return parseBlock();
    }

    return nullptr;
}

/* -------------------------------------------------------------------------- */

std::unique_ptr<AstLocalVarDef> Parser::parseLocalVarDef() {
    auto start_span = tok.span;
    want(TOK_LET);

    auto name_tok = wantAndGet(TOK_IDENT);

    Type* type = nullptr;
    std::unique_ptr<AstExpr> init;
    size_t arr_size = 0;
    TextSpan end_span;
    if (has(TOK_COLON)) {
        type = parseTypeExt(&arr_size);

        if (has(TOK_ASSIGN)) {
            if (arr_size > 0) {
                next();

                if (has(TOK_LBRACKET)) {
                    auto arr_lit = parseArrayLit();

                    if (arr_lit->elements.size() != arr_size) {
                        error(arr_lit->span, "array literal does not match declared array size");
                    }

                    init = std::move(arr_lit);
                    end_span = init->span;
                } else {
                    error(tok.span, "sized array declaration can only be initialized with array literal");
                }
            } else {
                init = parseInitializer();
                end_span = init->span;
            }

        } else {
            end_span = prev.span;
        }
    } else {
        init = parseInitializer();
        end_span = init->span;
    }

    Symbol* symbol = arena.New<Symbol>(
        arena.MoveStr(std::move(name_tok.value)),
        src_file.id,
        name_tok.span,
        SYM_VARIABLE,
        type,
        false
    );

    return std::make_unique<AstLocalVarDef>(
        SpanOver(start_span, end_span),
        symbol,
        std::move(init),
        arr_size
    );
}

std::unique_ptr<AstNode> Parser::parseExprAssignStmt() {
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

        return std::make_unique<AstIncDec>(span, std::move(lhs), AOP_ADD);
    } break;    
    case TOK_DEC:{
        next();
        auto span = SpanOver(lhs->span, prev.span);

        return std::make_unique<AstIncDec>(span, std::move(lhs), AOP_SUB);
    } break;
    default:
        return lhs;
    }

    auto rhs = parseExpr();

    auto span = SpanOver(lhs->span, rhs->span);
    return std::make_unique<AstAssign>(span, std::move(lhs), std::move(rhs), assign_op);
}
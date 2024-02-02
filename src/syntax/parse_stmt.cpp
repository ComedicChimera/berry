#include "parser.hpp"

std::unique_ptr<AstNode> Parser::parseStmt() {
    switch (tok.kind) {
    case TOK_LET:
        return parseLocalVarDef();
    default:
        return parseExpr();
    }
}

std::unique_ptr<AstLocalVarDef> Parser::parseLocalVarDef() {
    auto start_span = tok.span;
    want(TOK_LET);

    auto name_tok = wantAndGet(TOK_IDENT);

    Type* type = nullptr;
    std::unique_ptr<AstExpr> init;
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
        arena.MoveStr(std::move(name_tok.value)),
        src_file.id,
        name_tok.span,
        SYM_FUNC,
        type,
        false
    );

    return std::make_unique<AstLocalVarDef>(
        SpanOver(start_span, end_span),
        symbol,
        std::move(init)
    );
}
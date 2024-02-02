#include "parser.hpp"

std::unique_ptr<AstNode> Parser::parseBlock() {
    auto start_span = tok.span;
    want(TOK_LBRACE);

    std::vector<std::unique_ptr<AstNode>> stmts;
    while (!has(TOK_RBRACE)) {
        stmts.emplace_back(parseStmt());

        want(TOK_SEMI);
    }


    auto end_span = tok.span;
    want(TOK_RBRACE);
    return std::make_unique<AstBlock>(SpanOver(start_span, end_span), std::move(stmts));
}
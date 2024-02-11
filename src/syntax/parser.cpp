#include "parser.hpp"

void Parser::ParseFile() {
    next();

    Metadata meta;
    while (!has(TOK_EOF)) {
        if (has(TOK_ATSIGN)) {
            parseMetadata(meta);
        }

        parseDef(std::move(meta));
    }
}

/* -------------------------------------------------------------------------- */

std::vector<std::unique_ptr<AstExpr>> Parser::parseExprList(TokenKind delim) {
    std::vector<std::unique_ptr<AstExpr>> exprs;

    while (true) {
        exprs.emplace_back(parseExpr());

        if (has(delim)) {
            next();
        } else {
            break;
        }
    }

    return exprs;
}

std::unique_ptr<AstExpr> Parser::parseInitializer() {
    want(TOK_ASSIGN);
    return parseExpr();
}

std::vector<Token> Parser::parseIdentList(TokenKind delim) {
    std::vector<Token> toks;
    
    while (true) {
        toks.emplace_back(wantAndGet(TOK_IDENT));

        if (has(delim)) {
            next();
        } else {
            break;
        }
    }

    return toks;
}

/* -------------------------------------------------------------------------- */

void Parser::defineGlobal(Symbol* symbol) {
    auto it = src_file.parent->symbol_table.find(symbol->name);
    if (it == src_file.parent->symbol_table.end()) {
        src_file.parent->symbol_table[symbol->name] = symbol;
    } else {
        error(
            symbol->span, 
            "symbol named {} defined multiple times in same scope", 
            symbol->name
        );
    }
}

/* -------------------------------------------------------------------------- */

void Parser::next() {
    prev = std::move(tok);
    lexer.NextToken(tok);
}

bool Parser::has(TokenKind kind) {
    return tok.kind == kind;
}

void Parser::want(TokenKind kind) {
    if (has(kind)) {
        next();
    } else {
        reject("expected {}", tokKindToString(kind));
    }
}

Token Parser::wantAndGet(TokenKind kind) {
    if (has(kind)) {
        next();
        return prev;
    } else {
        reject("expected {}", tokKindToString(kind));
        return {};
    }
}

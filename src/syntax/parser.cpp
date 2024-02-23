#include "parser.hpp"

void Parser::ParseFile() {
    next();

    // Skip the module declaration if it is present: it should already have been
    // checked/parsed by the loader before this call was made.
    if (has(TOK_MODULE)) {
        next();  // module
        next();  // IDENT
        next();  // SEMI
    }

    MetadataMap meta;
    while (!has(TOK_EOF)) {
        if (has(TOK_ATSIGN)) {
            parseMetadata(meta);
        }

        parseDef(std::move(meta));
    }
}

Token Parser::ParseModuleName() {
    if (has(TOK_MODULE)) {
        next();

        auto name = wantAndGet(TOK_IDENT);
        want(TOK_SEMI);

        return name;
    }

    // No name specified.
    return { TOK_EOF };
}

/* -------------------------------------------------------------------------- */

std::span<AstExpr*> Parser::parseExprList(TokenKind delim) {
    std::vector<AstExpr*> exprs;

    while (true) {
        exprs.emplace_back(parseExpr());

        if (has(delim)) {
            next();
        } else {
            break;
        }
    }

    return arena.MoveVec(std::move(exprs));
}

AstExpr* Parser::parseInitializer() {
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

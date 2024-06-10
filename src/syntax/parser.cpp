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

    // Parse import statements.
    while (has(TOK_IMPORT)) {
        parseImportStmt();
    }

    // Mark runtime module as universally unsafe.
    DeclFlags global_flags = src_file.parent->id == BERRY_RT_MOD_ID ? DECL_UNSAFE : 0;

    // Parse remaining declarations in file.
    AttributeMap attr_map;
    while (!has(TOK_EOF)) {
        if (has(TOK_ATSIGN)) {
            parseAttrList(attr_map);
        }

        auto flags = global_flags;
        if (has(TOK_PUB)) { // `pub` modifier
            next();
            flags |= DECL_EXPORTED;
        }

        if (has(TOK_UNSAFE)) { // `unsafe` modifier
            next();
            flags |= DECL_UNSAFE;
        }

        parseDecl(std::move(attr_map), flags);
    }
}

/* -------------------------------------------------------------------------- */

void Parser::parseAttrList(AttributeMap& attr_map) {
    next();

    if (has(TOK_LBRACKET)) {
        next();

        while (true) {
            parseAttribute(attr_map);

            if (has(TOK_COMMA)) {
                next();
            } else {
                break;
            }
        }

        want(TOK_RBRACKET);
    } else {
        parseAttribute(attr_map);
    }
}

void Parser::parseAttribute(AttributeMap& attr_map) {
    auto name_tok = wantAndGet(TOK_IDENT);
    auto name = global_arena.MoveStr(std::move(name_tok.value));

    if (has(TOK_LPAREN)) {
        next();
        auto value_tok = wantAndGet(TOK_STRLIT);
        want(TOK_RPAREN);

        attr_map.emplace(name, Attribute{
            name,
            name_tok.span,
            global_arena.MoveStr(std::move(value_tok.value)),
            value_tok.span
        });
    } else {
        attr_map.emplace(name, Attribute{ 
            name,
            name_tok.span
        });
    }
}

/* -------------------------------------------------------------------------- */

std::span<AstNode*> Parser::parseExprList(TokenKind delim) {
    std::vector<AstNode*> exprs;

    while (true) {
        exprs.emplace_back(parseExpr());

        if (has(delim)) {
            next();
        } else {
            break;
        }
    }

    return ast_arena.MoveVec(std::move(exprs));
}

AstNode* Parser::parseInitializer() {
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

void Parser::pushAllowStructLit(bool allowed) {
    allow_struct_lit_stack.push_back(allowed);
}

void Parser::popAllowStructLit() {
    Assert(allow_struct_lit_stack.size() > 0, "pop on empty allow struct lit stack");

    allow_struct_lit_stack.pop_back();
}

bool Parser::shouldParseStructLit() {
    return allow_struct_lit_stack.empty() || allow_struct_lit_stack.back();
}

/* -------------------------------------------------------------------------- */

void Parser::defineGlobal(Symbol* symbol) {
    if (!src_file.parent->symbol_table.contains(symbol->name)) {
        src_file.parent->symbol_table.emplace(symbol->name, symbol);
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

    while (directives_enabled && tok.kind == TOK_DIRECTIVE) {
        directives_enabled = false;
        auto old_prev = prev;

        parseDirective();
        
        prev = old_prev;
        directives_enabled = true;
    }
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

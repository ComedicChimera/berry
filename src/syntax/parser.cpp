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

    while (has(TOK_IMPORT)) {
        parseImportStmt();
    }

    AttributeMap attr_map;
    bool exported = false;
    while (!has(TOK_EOF)) {
        if (has(TOK_ATSIGN)) {
            parseAttrList(attr_map);
        }

        if (has(TOK_PUB)) {
            next();
            exported = true;
        } else {
            exported = false;
        }

        parseDecl(std::move(attr_map), exported);
    }
}

/* -------------------------------------------------------------------------- */

AstNode* Parser::parseExprList(TokenKind delim) {
    std::vector<AstNode*> exprs;

    while (true) {
        exprs.emplace_back(parseExpr());

        if (has(delim)) {
            next();
        } else {
            break;
        }
    }

    auto* alist = allocNode(AST_EXPR_LIST, SpanOver(exprs[0]->span, exprs.back()->span));
    alist->an_ExprList.exprs = ast_arena.MoveVec(std::move(exprs));
    return alist;
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

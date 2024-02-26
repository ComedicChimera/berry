#include "parser.hpp"

void Parser::parseMetadata(MetadataMap& meta) {
    next();

    if (has(TOK_LBRACKET)) {
        next();

        while (true) {
            parseMetaTag(meta);

            if (has(TOK_COMMA)) {
                next();
            } else {
                break;
            }
        }

        want(TOK_RBRACKET);
    } else {
        parseMetaTag(meta);
    }
}

void Parser::parseMetaTag(MetadataMap& meta) {
    auto name_tok = wantAndGet(TOK_IDENT);
    auto name = arena.MoveStr(std::move(name_tok.value));

    if (has(TOK_LPAREN)) {
        next();
        auto value_tok = wantAndGet(TOK_STRLIT);
        want(TOK_RPAREN);

        meta.emplace(name, MetadataTag{
            name,
            name_tok.span,
            arena.MoveStr(std::move(value_tok.value)),
            value_tok.span
        });
    } else {
        meta.emplace(name, MetadataTag{ 
            name,
            name_tok.span
        });
    }
}

/* -------------------------------------------------------------------------- */

void Parser::parseDef(MetadataMap&& meta, bool exported) {
    switch (tok.kind) {
    case TOK_FUNC:
        parseFuncDef(std::move(meta), exported);
        break;
    case TOK_LET:
        parseGlobalVarDef(std::move(meta), exported);
        break;
    default:
        reject("expected global definition");
        break;
    }
}

/* -------------------------------------------------------------------------- */

void Parser::parseFuncDef(MetadataMap&& meta, bool exported) {
    auto start_span = tok.span;
    want(TOK_FUNC);

    auto name_tok = wantAndGet(TOK_IDENT);

    std::vector<Symbol*> params;
    want(TOK_LPAREN);
    if (!has(TOK_RPAREN)) {
        parseFuncParams(params);
    }
    want(TOK_RPAREN);
    
    Type* return_type;
    switch (tok.kind) {
    case TOK_SEMI: case TOK_LBRACE:
        return_type = &prim_unit_type;
        break;
    default:
        return_type = parseTypeLabel();
        break;
    }

    AstStmt* body { nullptr };
    TextSpan end_span;
    switch (tok.kind) {
    case TOK_SEMI:
        end_span = tok.span;
        next();
        break;
    case TOK_LBRACE:
        body = parseBlock();
        end_span = body->span;
        break;
    default:
        reject("expected semicolon or function body");
        break;
    }

    std::vector<Type*> param_types;
    for (auto* param_symbol : params) {
        param_types.push_back(param_symbol->type);
    }

    auto* func_type = allocType(TYPE_FUNC);
    func_type->ty_Func.param_types = arena.MoveVec(std::move(param_types));
    func_type->ty_Func.return_type = return_type;

    Symbol* symbol = arena.New<Symbol>(
        src_file.parent->id,
        arena.MoveStr(std::move(name_tok.value)),
        name_tok.span,
        SYM_FUNC,
        func_type,
        true,
        exported ? src_file.parent->export_table.size() : UNEXPORTED
    );

    defineGlobal(symbol);

    auto* afunc = allocDef(AST_FUNC, SpanOver(start_span, end_span), std::move(meta));
    afunc->an_Func.symbol = symbol;
    afunc->an_Func.params = arena.MoveVec(std::move(params));
    afunc->an_Func.return_type = return_type;
    afunc->an_Func.body = body;

    src_file.defs.push_back(afunc);

    if (exported) {
        src_file.parent->export_table.emplace_back(afunc, 0);
    }
}

void Parser::parseFuncParams(std::vector<Symbol*>& params) {
    while (true) {
        auto name_toks = parseIdentList();
        Type* type = parseTypeExt();

        for (auto& name_tok : name_toks) {
            Symbol* symbol = arena.New<Symbol>(
                src_file.parent->id,
                arena.MoveStr(std::move(name_tok.value)),
                name_tok.span,
                SYM_VARIABLE,
                type,
                false
            );

            params.push_back(symbol);
        }
        
        if (has(TOK_COMMA)) {
            next();
        } else {
            break;
        }
    }
}

/* -------------------------------------------------------------------------- */

void Parser::parseGlobalVarDef(MetadataMap&& meta, bool exported) {
    auto start_span = tok.span;
    next();

    auto name_tok = wantAndGet(TOK_IDENT);

    if (!has(TOK_COLON)) {
        fatal(name_tok.span, "global variable must have an explicit type label");
    }

    auto* type = parseTypeExt();

    AstExpr* init = nullptr;
    if (has(TOK_ASSIGN)) {
        init = parseInitializer();
    }

    auto end_span = tok.span;
    want(TOK_SEMI);

    Symbol* symbol = arena.New<Symbol>(
        src_file.parent->id,
        arena.MoveStr(std::move(name_tok.value)),
        name_tok.span,
        SYM_VARIABLE,
        type,
        false,
        exported ? src_file.parent->export_table.size() : UNEXPORTED
    );

    defineGlobal(symbol);

    auto* aglobal = allocDef(AST_GLOBAL_VAR, SpanOver(start_span, end_span), std::move(meta));
    aglobal->an_GlobalVar.symbol = symbol;
    aglobal->an_GlobalVar.init = init;

    src_file.defs.push_back(aglobal);

    if (exported) {
        src_file.parent->export_table.emplace_back(aglobal, 0);
    }
}
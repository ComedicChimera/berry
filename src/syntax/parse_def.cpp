#include "parser.hpp"

void Parser::parseMetadata(Metadata& meta) {
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

void Parser::parseMetaTag(Metadata& meta) {
    auto name_tok = wantAndGet(TOK_IDENT);
    auto name = name_tok.value;

    if (has(TOK_LPAREN)) {
        next();
        auto value_tok = wantAndGet(TOK_STRLIT);
        want(TOK_RPAREN);

        meta.emplace(name, MetadataTag{
            std::move(name_tok.value),
            name_tok.span,
            std::move(value_tok.value),
            value_tok.span
        });
    } else {
        meta.emplace(name, MetadataTag{ 
            std::move(name_tok.value),
            name_tok.span
        });
    }
}

/* -------------------------------------------------------------------------- */

void Parser::parseDef(Metadata&& meta) {
    switch (tok.kind) {
    case TOK_FUNC:
        parseFuncDef(std::move(meta));
        break;
    case TOK_LET:
        parseGlobalVarDef(std::move(meta));
        break;
    default:
        reject("expected global definition");
        break;
    }
}

/* -------------------------------------------------------------------------- */

void Parser::parseFuncDef(Metadata&& meta) {
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

    std::unique_ptr<AstNode> body;
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

    FuncType* func_type = arena.New<FuncType>(
        arena.MoveVec(std::move(param_types)),
        return_type
    );    

    Symbol* symbol = arena.New<Symbol>(
        arena.MoveStr(std::move(name_tok.value)),
        src_file.id,
        name_tok.span,
        SYM_FUNC,
        func_type,
        true
    );

    defineGlobal(symbol);

    src_file.defs.emplace_back(std::make_unique<AstFuncDef>(
        SpanOver(start_span, end_span),
        std::move(meta),
        symbol,
        std::move(params),
        return_type,
        std::move(body)
    ));
}

void Parser::parseFuncParams(std::vector<Symbol*>& params) {
    while (true) {
        auto name_toks = parseIdentList();
        Type* type = parseTypeExt();

        for (auto& name_tok : name_toks) {
            Symbol* symbol = arena.New<Symbol>(
                arena.MoveStr(std::move(name_tok.value)),
                src_file.id,
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

void Parser::parseGlobalVarDef(Metadata&& meta) {
    auto local_var = parseLocalVarDef();
    want(TOK_SEMI);

    if (local_var->symbol->type == nullptr) {
        fatal(local_var->span, "global variable must have an explicit type label");
    }

    defineGlobal(local_var->symbol);

    src_file.defs.emplace_back(
        std::make_unique<AstGlobalVarDef>(std::move(meta), std::move(local_var))
    );
}
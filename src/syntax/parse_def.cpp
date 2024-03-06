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
        parseGlobalVarDef(std::move(meta), exported, false);
        break;
    case TOK_CONST:
        parseGlobalVarDef(std::move(meta), exported, true);
        break;
    case TOK_STRUCT:
        parseStructDef(std::move(meta), exported);
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
        exported ? SYM_FUNC | SYM_EXPORTED : SYM_FUNC,
        func_type,
        true
    );

    defineGlobal(symbol, src_file.defs.size());

    auto* afunc = allocDef(AST_FUNC, SpanOver(start_span, end_span), std::move(meta));
    afunc->an_Func.symbol = symbol;
    afunc->an_Func.params = arena.MoveVec(std::move(params));
    afunc->an_Func.return_type = return_type;
    afunc->an_Func.body = body;

    src_file.defs.push_back(afunc);
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
                SYM_VAR,
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

void Parser::parseGlobalVarDef(MetadataMap&& meta, bool exported, bool immut) {
    auto start_span = tok.span;
    next();

    auto name_tok = wantAndGet(TOK_IDENT);

    if (!has(TOK_COLON)) {
        fatal(name_tok.span, "global variable must have an explicit type label");
    }

    auto* type = parseTypeExt();

    AstExpr* init_expr = nullptr;
    if (has(TOK_ASSIGN)) {
        init_expr = parseInitializer();
    }

    auto end_span = tok.span;
    want(TOK_SEMI);

    Symbol* symbol = arena.New<Symbol>(
        src_file.parent->id,
        arena.MoveStr(std::move(name_tok.value)),
        name_tok.span,
        exported ? SYM_VAR | SYM_EXPORTED : SYM_VAR,
        type,
        immut  
    );

    defineGlobal(symbol, src_file.parent->global_vars.size());

    auto meta_tags = moveMetadataToArena(std::move(meta));
    auto* aglobal = arena.New<AstGlobalVar>(
        &src_file,
        meta_tags,

        symbol,
        init_expr,
        false
    );

    src_file.parent->global_vars.push_back(aglobal);
}

/* -------------------------------------------------------------------------- */

void Parser::parseStructDef(MetadataMap&& meta, bool exported) {
    auto start_span = tok.span;
    next();

    auto name_tok = wantAndGet(TOK_IDENT);

    want(TOK_LBRACE);

    bool field_exported = false;
    std::vector<StructField> fields;
    std::unordered_set<std::string_view> used_field_names;
    do {
        // TODO: field metadata

        if (has(TOK_PUB)) {
            if (!exported) {
                error(tok.span, "unexported struct cannot have exported fields");
            }

            next();
            field_exported = true;
        } else {
            field_exported = false;
        }

        auto field_name_toks = parseIdentList();
        auto field_type = parseTypeExt();

        for (auto& field_name_tok : field_name_toks) {
            auto field_name = arena.MoveStr(std::move(field_name_tok.value));

            if (used_field_names.contains(field_name)) {
                error(field_name_tok.span, "multiple fields named {}", field_name);
            }

            used_field_names.insert(field_name);
            fields.emplace_back(StructField{
                field_name,
                field_type,
                field_exported
            });
        }

        want(TOK_SEMI);
    } while (!has(TOK_RBRACE));
    next();

    auto struct_type = allocType(TYPE_STRUCT);
    struct_type->ty_Struct.fields = arena.MoveVec(std::move(fields));
    struct_type->ty_Struct.llvm_type = nullptr;
    
    auto named_type = allocType(TYPE_NAMED);
    named_type->ty_Named.mod_id = src_file.parent->id;
    named_type->ty_Named.mod_name = src_file.parent->name;  // No need to move to arena here.
    named_type->ty_Named.name = arena.MoveStr(std::move(name_tok.value));
    named_type->ty_Named.type = struct_type;

    auto* symbol = arena.New<Symbol>(
        src_file.parent->id,
        named_type->ty_Named.name,
        name_tok.span,
        exported ? SYM_TYPE | SYM_EXPORTED : SYM_TYPE,
        named_type,
        false
    );

    defineGlobal(symbol, src_file.defs.size());

    auto* astruct = allocDef(AST_STRUCT, SpanOver(start_span, prev.span), std::move(meta));
    astruct->an_Struct.symbol = symbol;
    astruct->an_Struct.field_attrs = {};

    src_file.defs.push_back(astruct);
}
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
    case TOK_CONST:
        parseGlobalVarDef(std::move(meta), exported);
        break;
    case TOK_STRUCT:
        parseStructDef(std::move(meta), exported);
        break;
    case TOK_TYPE:
        parseAliasDef(std::move(meta), exported);
        break;
    case TOK_ENUM:
        parseEnumDef(std::move(meta), exported);
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
        src_file.parent->defs.size(),
        func_type,
        true
    );

    defineGlobal(symbol);

    auto* afunc = allocDef(AST_FUNC, SpanOver(start_span, end_span), std::move(meta));
    afunc->an_Func.symbol = symbol;
    afunc->an_Func.params = arena.MoveVec(std::move(params));
    afunc->an_Func.return_type = return_type;
    afunc->an_Func.body = body;

    src_file.parent->defs.push_back(afunc);
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
                0,
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
    bool comptime = tok.kind == TOK_CONST;
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

    SymbolFlags flags = comptime ? SYM_CONST : SYM_VAR;
    if (exported)
        flags |= SYM_EXPORTED;

    Symbol* symbol = arena.New<Symbol>(
        src_file.parent->id,
        arena.MoveStr(std::move(name_tok.value)),
        name_tok.span,
        flags,
        src_file.parent->defs.size(),
        type,
        comptime  
    );

    defineGlobal(symbol);

    auto* aglobal = allocDef(AST_GLVAR, SpanOver(start_span, end_span), std::move(meta));
    aglobal->an_GlVar.symbol = symbol;
    aglobal->an_GlVar.init_expr = init_expr;
    aglobal->an_GlVar.const_value = nullptr;

    src_file.parent->defs.push_back(aglobal);
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
        src_file.parent->defs.size(),
        named_type,
        false
    );

    defineGlobal(symbol);

    auto* astruct = allocDef(AST_STRUCT, SpanOver(start_span, prev.span), std::move(meta));
    astruct->an_Struct.symbol = symbol;

    src_file.parent->defs.push_back(astruct);
}

void Parser::parseAliasDef(MetadataMap&& meta, bool exported) {
    auto start_span = tok.span;
    next();

    auto ident = wantAndGet(TOK_IDENT);

    want(TOK_ASSIGN);

    auto* base_type = parseTypeLabel();

    want(TOK_SEMI);

    auto* alias_type = allocType(TYPE_ALIAS);
    alias_type->ty_Named.mod_id = src_file.parent->id;
    alias_type->ty_Named.mod_name = src_file.parent->name;
    alias_type->ty_Named.name = arena.MoveStr(std::move(ident.value));
    alias_type->ty_Named.type = base_type;

    auto* symbol = arena.New<Symbol>(
        src_file.parent->id,
        alias_type->ty_Named.name,
        ident.span,
        exported ? SYM_TYPE | SYM_EXPORTED : SYM_TYPE,
        src_file.parent->defs.size(),
        alias_type,
        false
    );

    defineGlobal(symbol);

    auto* aalias = allocDef(AST_ALIAS, SpanOver(start_span, prev.span), std::move(meta));
    aalias->an_Alias.symbol = symbol;
    
    src_file.parent->defs.push_back(aalias);
}

void Parser::parseEnumDef(MetadataMap&& meta, bool exported) {
    auto start_span = tok.span;
    next();

    auto ident = wantAndGet(TOK_IDENT);

    want(TOK_LBRACE);

    std::unordered_map<std::string_view, EnumVariant> variants;
    std::vector<AstVariantInit> variant_inits;
    do {
        auto var_name_tok = wantAndGet(TOK_IDENT);

        AstExpr* variant_init_expr { nullptr };
        if (has(TOK_ASSIGN)) {
            variant_init_expr = parseInitializer();
        }
        want(TOK_SEMI);

        auto variant_name = arena.MoveStr(std::move(var_name_tok.value));
        auto it = variants.find(variant_name);
        if (it != variants.end()) {
            error(var_name_tok.span, "multiple variants named {}", variant_name);
        } else {
            variants.emplace(variant_name, EnumVariant{ variants.size(), nullptr });
            variant_inits.emplace_back(variant_init_expr);
        }
    } while (!has(TOK_RBRACE));
    next();

    auto* enum_type = allocType(TYPE_ENUM);
    enum_type->ty_Enum.variants = MapView<EnumVariant>(arena, std::move(variants));

    auto* named_type = allocType(TYPE_NAMED);
    named_type->ty_Named.mod_id = src_file.parent->id;
    named_type->ty_Named.mod_name = src_file.parent->name;
    named_type->ty_Named.name = arena.MoveStr(std::move(ident.value));
    named_type->ty_Named.type = enum_type;

    auto* symbol = arena.New<Symbol>(
        src_file.parent->id,
        named_type->ty_Named.name,
        ident.span,
        exported ? SYM_TYPE | SYM_EXPORTED : SYM_TYPE,
        src_file.parent->defs.size(),
        named_type,
        false
    );

    defineGlobal(symbol);

    auto* aenum = allocDef(AST_ENUM, SpanOver(start_span, prev.span), std::move(meta));
    aenum->an_Enum.symbol = symbol;
    aenum->an_Enum.variant_inits = arena.MoveVec(std::move(variant_inits));

    src_file.parent->defs.push_back(aenum);
}
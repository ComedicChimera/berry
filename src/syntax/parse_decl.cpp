#include "parser.hpp"

void Parser::parseDecl(AttributeMap&& attr_map, DeclFlags flags) {
    AstNode* node { nullptr };

    bool exported = (flags & DECL_EXPORTED) > 0;
    switch (tok.kind) {
    case TOK_FUNC:
        node = parseFuncOrMethodDecl(exported);
        break;
    case TOK_FACTORY:
        node = parseFactoryDecl(exported);
        break;
    case TOK_LET:
    case TOK_CONST:
        node = parseGlobalVarDecl(exported);
        break;
    case TOK_STRUCT:
        node = parseStructDecl(exported);
        break;
    case TOK_TYPE:
        node = parseAliasDecl(exported);
        break;
    case TOK_ENUM:
        node = parseEnumDecl(exported);
        break;
    default:
        reject("expected global definition");
        break;
    }
    
    auto* decl = global_arena.New<Decl>(
        src_file.file_num, 
        flags,
        moveAttrsToArena(std::move(attr_map)), 
        node
    );

    src_file.parent->decls.push_back(decl);
}

/* -------------------------------------------------------------------------- */

AstNode* Parser::parseFuncOrMethodDecl(bool exported) {
    auto start_span = tok.span;
    want(TOK_FUNC);

    auto name_tok = wantAndGet(TOK_IDENT);

    AstNode* bind_type = nullptr;
    if (has(TOK_DOT)) {
        bind_type = allocNode(AST_IDENT, name_tok.span);
        bind_type->an_Ident.name = ast_arena.MoveStr(std::move(name_tok.value));

        next();
        name_tok = wantAndGet(TOK_IDENT);

        if (has(TOK_DOT)) {
            auto* sel_type = allocNode(AST_SELECTOR, SpanOver(bind_type->span, name_tok.span));
            sel_type->an_Sel.expr = bind_type;
            sel_type->an_Sel.field_name = ast_arena.MoveStr(std::move(name_tok.value));
            bind_type = sel_type;

            next();
            name_tok = wantAndGet(TOK_IDENT);
        }
    }

    auto* afunc_type = parseFuncSignature();

    AstNode* body { nullptr };
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

    if (bind_type != nullptr) {
        auto* amethod = allocNode(AST_METHOD, SpanOver(start_span, end_span));
        amethod->an_Method.bind_type = bind_type;
        amethod->an_Method.name = global_arena.MoveStr(std::move(name_tok.value));
        amethod->an_Method.name_span = name_tok.span;
        amethod->an_Method.func_type = afunc_type;
        amethod->an_Method.body = body;
        return amethod;
    }

    Symbol* symbol = global_arena.New<Symbol>(
        src_file.parent->id,
        global_arena.MoveStr(std::move(name_tok.value)),
        name_tok.span,
        exported ? SYM_FUNC | SYM_EXPORTED : SYM_FUNC,
        src_file.parent->decls.size(),
        nullptr,
        true
    );

    defineGlobal(symbol);

    auto* afunc = allocNode(AST_FUNC, SpanOver(start_span, end_span));
    afunc->an_Func.symbol = symbol;
    afunc->an_Func.func_type = afunc_type;
    afunc->an_Func.body = body;
   
    return afunc;
}

AstNode* Parser::parseFactoryDecl(bool exported) {
    auto start_span = tok.span;
    want(TOK_FACTORY);

    auto name_tok = wantAndGet(TOK_IDENT);
    auto* bind_type = allocNode(AST_IDENT, name_tok.span);
    bind_type->an_Ident.name = ast_arena.MoveStr(std::move(name_tok.value));

    if (has(TOK_DOT)) {
        next();
        name_tok = wantAndGet(TOK_IDENT);

        auto* sel_type = allocNode(AST_SELECTOR, SpanOver(bind_type->span, name_tok.span));
        sel_type->an_Sel.expr = bind_type;
        sel_type->an_Sel.field_name = ast_arena.MoveStr(std::move(name_tok.value));
        bind_type = sel_type;
    }

    auto* afunc_type = parseFuncSignature();
    auto* body = parseBlock();

    auto* afactory = allocNode(AST_FACTORY, SpanOver(start_span, body->span));
    afactory->an_Factory.bind_type = bind_type;
    afactory->an_Factory.func_type = afunc_type;
    afactory->an_Factory.body = body;

    return afactory;
}

AstNode* Parser::parseFuncSignature() {
    auto start_span = tok.span;

    std::vector<AstFuncParam> params;
    want(TOK_LPAREN);
    if (!has(TOK_RPAREN)) {
        parseFuncParams(params);
    }
    want(TOK_RPAREN);
    
    AstNode* return_type { nullptr };
    if (tok.kind != TOK_SEMI && tok.kind != TOK_LBRACE) {
        return_type = parseTypeLabel();
    }

    auto* afunc_type = allocNode(AST_TYPE_FUNC, SpanOver(start_span, prev.span));
    afunc_type->an_TypeFunc.params = ast_arena.MoveVec(std::move(params));
    afunc_type->an_TypeFunc.return_type = return_type;
    return afunc_type;
}

void Parser::parseFuncParams(std::vector<AstFuncParam>& params) {
    std::unordered_set<std::string_view> param_names;
    while (true) {
        auto name_toks = parseIdentList();
        auto* type = parseTypeExt();

        for (auto& name_tok : name_toks) {
            AstFuncParam aparam {
                SpanOver(name_tok.span, type->span),
                global_arena.MoveStr(std::move(name_tok.value)),
                type
            };

            if (param_names.contains(aparam.name)) {
                error(name_tok.span, "multiple parameters named {}", aparam.name);
            }

            params.push_back(aparam);
            param_names.insert(aparam.name);
        }
        
        if (has(TOK_COMMA)) {
            next();
        } else {
            break;
        }
    }
}

/* -------------------------------------------------------------------------- */

AstNode* Parser::parseGlobalVarDecl(bool exported) {
    auto* avar = parseLocalVarDecl();
    want(TOK_SEMI);

    auto* symbol = avar->an_Var.symbol;
    if (exported)
        symbol->flags |= SYM_EXPORTED;

    symbol->decl_num = src_file.parent->decls.size();
    defineGlobal(symbol);
    return avar;
}

/* -------------------------------------------------------------------------- */

AstNode* Parser::parseStructDecl(bool exported) {
    auto start_span = tok.span;
    next();

    auto name_tok = wantAndGet(TOK_IDENT);

    want(TOK_LBRACE);

    bool field_exported = false;
    std::vector<AstStructField> fields;
    std::unordered_map<std::string_view, size_t> name_map;
    do {
        // TODO: field attrs

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
            auto field_name = global_arena.MoveStr(std::move(field_name_tok.value));

            if (name_map.contains(field_name)) {
                error(field_name_tok.span, "multiple fields named {}", field_name);
            }

            name_map.emplace(field_name, fields.size());
            fields.emplace_back(AstStructField{
                SpanOver(field_name_tok.span, field_type->span),
                field_name,
                field_type,
                field_exported
            });
        }

        want(TOK_SEMI);
    } while (!has(TOK_RBRACE));
    next();
    
    auto named_type = AllocType(global_arena, TYPE_NAMED);
    named_type->ty_Named.mod_id = src_file.parent->id;
    named_type->ty_Named.mod_name = src_file.parent->name;  // No need to move to arena here.
    named_type->ty_Named.name = global_arena.MoveStr(std::move(name_tok.value));
    named_type->ty_Named.type = nullptr;
    named_type->ty_Named.methods = nullptr;
    named_type->ty_Named.factory = nullptr;

    auto* symbol = global_arena.New<Symbol>(
        src_file.parent->id,
        named_type->ty_Named.name,
        name_tok.span,
        exported ? SYM_TYPE | SYM_EXPORTED : SYM_TYPE,
        src_file.parent->decls.size(),
        named_type,
        false
    );

    defineGlobal(symbol);

    auto* astruct_type = allocNode(AST_TYPE_STRUCT, SpanOver(start_span, prev.span));
    astruct_type->an_TypeStruct.fields = ast_arena.MoveVec(std::move(fields));

    auto* astruct = allocNode(AST_TYPEDEF, SpanOver(start_span, prev.span));
    astruct->an_TypeDef.symbol = symbol;
    astruct->an_TypeDef.type = astruct_type;

    return astruct;
}

AstNode* Parser::parseAliasDecl(bool exported) {
    auto start_span = tok.span;
    next();

    auto ident = wantAndGet(TOK_IDENT);

    want(TOK_ASSIGN);

    auto* base_type = parseTypeLabel();

    want(TOK_SEMI);

    auto* alias_type = AllocType(global_arena, TYPE_ALIAS);
    alias_type->ty_Named.mod_id = src_file.parent->id;
    alias_type->ty_Named.mod_name = src_file.parent->name;
    alias_type->ty_Named.name = global_arena.MoveStr(std::move(ident.value));
    alias_type->ty_Named.type = nullptr;
    alias_type->ty_Named.methods = nullptr;
    alias_type->ty_Named.factory = nullptr;

    auto* symbol = global_arena.New<Symbol>(
        src_file.parent->id,
        alias_type->ty_Named.name,
        ident.span,
        exported ? SYM_TYPE | SYM_EXPORTED : SYM_TYPE,
        src_file.parent->decls.size(),
        alias_type,
        false
    );

    defineGlobal(symbol);

    auto* aalias = allocNode(AST_TYPEDEF, SpanOver(start_span, prev.span));
    aalias->an_TypeDef.symbol = symbol;
    aalias->an_TypeDef.type = base_type;
    
    return aalias;
}

AstNode* Parser::parseEnumDecl(bool exported) {
    auto start_span = tok.span;
    next();

    auto ident = wantAndGet(TOK_IDENT);

    want(TOK_LBRACE);

    std::unordered_map<std::string_view, size_t> name_map;
    std::vector<AstNode*> variants;
    do {
        auto var_name_tok = wantAndGet(TOK_IDENT);

        AstNode* variant_init_expr { nullptr };
        if (has(TOK_ASSIGN)) {
            variant_init_expr = parseInitializer();
        }
        want(TOK_SEMI);

        auto variant_name = global_arena.MoveStr(std::move(var_name_tok.value));
        if (name_map.contains(variant_name)) {
            error(var_name_tok.span, "multiple variants named {}", variant_name);
            continue;
        } 
        
        AstNode* variant;
        if (variant_init_expr) {
            variant = allocNode(AST_NAMED_INIT, SpanOver(var_name_tok.span, variant_init_expr->span));
            variant->an_NamedInit.name = variant_name;
            variant->an_NamedInit.init = variant_init_expr;
        } else {
            variant = allocNode(AST_IDENT, var_name_tok.span);
            variant->an_Ident.name = variant_name;
        }

        variants.push_back(variant);
    } while (!has(TOK_RBRACE));
    next();

    auto* named_type = AllocType(global_arena, TYPE_NAMED);
    named_type->ty_Named.mod_id = src_file.parent->id;
    named_type->ty_Named.mod_name = src_file.parent->name;
    named_type->ty_Named.name = global_arena.MoveStr(std::move(ident.value));
    named_type->ty_Named.type = nullptr;
    named_type->ty_Named.methods = nullptr;
    named_type->ty_Named.factory = nullptr;

    auto* symbol = global_arena.New<Symbol>(
        src_file.parent->id,
        named_type->ty_Named.name,
        ident.span,
        exported ? SYM_TYPE | SYM_EXPORTED : SYM_TYPE,
        src_file.parent->decls.size(),
        named_type,
        false
    );

    defineGlobal(symbol);

    auto* aenum_type = allocNode(AST_TYPE_ENUM, SpanOver(start_span, prev.span));
    aenum_type->an_TypeEnum.variants = ast_arena.MoveVec(std::move(variants));    

    auto* aenum = allocNode(AST_TYPEDEF, SpanOver(start_span, prev.span));
    aenum->an_TypeDef.symbol = symbol;
    aenum->an_TypeDef.type = aenum_type;

    return aenum;
}
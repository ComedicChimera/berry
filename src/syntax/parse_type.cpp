#include "parser.hpp"

AstNode* Parser::parseTypeExt() {
    want(TOK_COLON);

    return parseTypeLabel();
}

AstNode* Parser::parseTypeLabel() {
    Type* prim_type;
    switch (tok.kind) {
    case TOK_I8:
        prim_type = &prim_i8_type;
        break;
    case TOK_U8:
        prim_type = &prim_u8_type;
        break;
    case TOK_I16:
        prim_type = &prim_i16_type;
        break;
    case TOK_U16:
        prim_type = &prim_u16_type;
        break;
    case TOK_I32:
        prim_type = &prim_i32_type;
        break;
    case TOK_U32:
        prim_type = &prim_u32_type;
        break;
    case TOK_I64:
        prim_type = &prim_i64_type;
        break;
    case TOK_U64:
        prim_type = &prim_u64_type;
        break;
    case TOK_F32:
        prim_type = &prim_f32_type;
        break;
    case TOK_F64:
        prim_type = &prim_f64_type;
        break;
    case TOK_BOOL:
        prim_type = &prim_bool_type;
        break;
    case TOK_UNIT:
        prim_type = &prim_unit_type;
        break;
    case TOK_STRING:
        prim_type = &prim_string_type;
        break;
    case TOK_STAR: {
        auto start_span = tok.span;
        next();

        auto* aelem_type = parseTypeLabel();
        auto* aptr_type = allocNode(AST_DEREF, SpanOver(start_span, aelem_type->span));
        aptr_type->an_Deref.expr = aelem_type;
        return aptr_type;
    } break;
    case TOK_LBRACKET: {
        auto start_span = tok.span;
        next();

        if (has(TOK_RBRACKET)) {
            next();

            auto* aelem_type = parseTypeLabel();

            auto* aslice_type = allocNode(AST_TYPE_SLICE, SpanOver(start_span, aelem_type->span));
            aslice_type->an_TypeSlice.elem_type = aelem_type;
            return aslice_type;
        } else {
            auto* len_expr = parseExpr();
            want(TOK_RBRACKET);

            auto* aelem_type = parseTypeLabel();

            auto* aarray_type = allocNode(AST_TYPE_ARRAY, SpanOver(start_span, aelem_type->span));
            aarray_type->an_TypeArray.elem_type = aelem_type;
            aarray_type->an_TypeArray.len = len_expr;
            return aarray_type;
        }
    } break;
    case TOK_STRUCT:
        return parseStructTypeLabel();
    case TOK_IDENT: {
        next();

        auto* aident = allocNode(AST_IDENT, prev.span);
        aident->an_Ident.name = ast_arena.MoveStr(std::move(prev.value));
        
        if (has(TOK_DOT)) {
            next();

            want(TOK_IDENT);

            auto* asel = allocNode(AST_SELECTOR, SpanOver(aident->span, prev.span));
            asel->an_Sel.expr = aident;
            asel->an_Sel.field_name = ast_arena.MoveStr(std::move(prev.value));
            return asel;
        }

        return aident;
    } break;
    default:
        reject("expected type label");
        return nullptr;
    }

    next();

    auto* aprim_type = allocNode(AST_TYPE_PRIM, prev.span);
    aprim_type->an_TypePrim.prim_type = prim_type;
    return aprim_type;
}

AstNode* Parser::parseStructTypeLabel() {
    next();
    auto start_span = prev.span;

    want(TOK_LBRACE);

    std::vector<AstStructField> fields;
    std::unordered_map<std::string_view, size_t> name_map;
    while (true) {
        auto field_name_toks = parseIdentList();
        auto field_type = parseTypeExt();
        
        for (auto& field_name_tok : field_name_toks) {
            auto field_name = global_arena.MoveStr(std::move(field_name_tok.value));

            if (name_map.contains(field_name)) {
                error(field_name_tok.span, "multiple field named {}", field_name);
            }

            name_map.emplace(field_name, fields.size());
            fields.emplace_back(AstStructField{ field_name_tok.span, field_name, field_type, true });
        }

        if (has(TOK_COMMA)) {
            next();
        } else {
            break;
        }
    } 
    want(TOK_RBRACE);

    auto* astruct_type = allocNode(AST_TYPE_STRUCT, SpanOver(start_span, prev.span));
    astruct_type->an_TypeStruct.fields = ast_arena.MoveVec(std::move(fields));
    return astruct_type;
}
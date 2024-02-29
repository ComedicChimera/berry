#include "parser.hpp"

Type* Parser::parseTypeExt() {
    want(TOK_COLON);

    return parseTypeLabel();
}

Type* Parser::parseTypeLabel() {
    switch (tok.kind) {
    case TOK_I8:
        next();
        return &prim_i8_type;
    case TOK_U8:
        next();
        return &prim_u8_type;
    case TOK_I16:
        next();
        return &prim_i16_type;
    case TOK_U16:
        next();
        return &prim_u16_type;
    case TOK_I32:
        next();
        return &prim_i32_type;
    case TOK_U32:
        next();
        return &prim_u32_type;
    case TOK_I64:
        next();
        return &prim_i64_type;
    case TOK_U64:
        next();
        return &prim_u64_type;
    case TOK_F32:
        next();
        return &prim_f32_type;
    case TOK_F64:
        next();
        return &prim_f64_type;
    case TOK_BOOL:
        next();
        return &prim_bool_type;
    case TOK_UNIT:
        next();
        return &prim_unit_type;
    case TOK_STAR: {
        next();
        auto* ptr_type = allocType(TYPE_PTR);
        ptr_type->ty_Ptr.elem_type = parseTypeLabel();
        return ptr_type;
    } break;
    case TOK_LBRACKET: {
        next();
        want(TOK_RBRACKET);

        auto* arr_type = allocType(TYPE_ARRAY);
        arr_type->ty_Array.elem_type = parseTypeLabel();
        return arr_type;
    } break;
    case TOK_STRUCT:
        return parseStructTypeLabel();
    default:
        reject("expected type label");
        return nullptr;
    }
}

Type* Parser::parseStructTypeLabel() {
    next();
    want(TOK_LBRACE);

    std::vector<StructField> fields;
    std::unordered_set<std::string_view> used_field_names;
    do {
        auto field_name_toks = parseIdentList();
        auto field_type = parseTypeExt();
        
        for (auto& field_name_tok : field_name_toks) {
            auto field_name = arena.MoveStr(std::move(field_name_tok.value));

            if (used_field_names.contains(field_name)) {
                error(field_name_tok.span, "multiple field named {}", field_name);
            }

            used_field_names.insert(field_name);
            fields.emplace_back(StructField{ field_name, field_type, true });
        }

        want(TOK_SEMI);
    } while (!has(TOK_RBRACE));
    next();

    auto* struct_type = allocType(TYPE_STRUCT);
    struct_type->ty_Struct.fields = arena.MoveVec(std::move(fields));

    return struct_type;
}
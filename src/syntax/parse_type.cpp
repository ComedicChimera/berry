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
    case TOK_STRING:
        next();
        return &prim_string_type;
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
    case TOK_IDENT:
        return parseNamedTypeLabel();
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
    while (true) {
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

        if (has(TOK_COMMA)) {
            next();
        } else {
            break;
        }
    } 
    want(TOK_RBRACE);

    auto* struct_type = allocType(TYPE_STRUCT);
    struct_type->ty_Struct.fields = arena.MoveVec(std::move(fields));
    struct_type->ty_Struct.llvm_type = nullptr;

    return struct_type;
}

Type* Parser::parseNamedTypeLabel() {
    next();

    NamedTypeTable::Ref* ref;
    bool first_ref = false;
    if (has(TOK_DOT)) {
        auto mod_name_tok = wantAndGet(TOK_IDENT);

        auto mod_name = arena.MoveStr(std::move(mod_name_tok.value));
        auto it_dep = src_file.import_table.find(mod_name);
        if (it_dep == src_file.import_table.end()) {
            fatal(mod_name_tok.span, "no module imported with name {}", mod_name);
        }

        auto& refs = src_file.parent->named_table.external_refs[it_dep->second];

        auto it_ref = refs.find(prev.value);
        if (it_ref == refs.end()) {
            ref = &refs.emplace(prev.value, NamedTypeTable::Ref{}).first->second;
            first_ref = true;
        } else {
            ref = &it_ref->second;
        }
    } else {
        auto& refs = src_file.parent->named_table.internal_refs;
        
        auto it = refs.find(prev.value);
        if (it == refs.end()) {
            ref = &refs.emplace(prev.value, NamedTypeTable::Ref{}).first->second;
            first_ref = true;
        } else {
            ref = &it->second;
        }
    }

    if (first_ref) {
        auto* named_type = allocType(TYPE_NAMED);
        named_type->ty_Named.type = nullptr;
        named_type->ty_Named.name = arena.MoveStr(std::move(prev.value));

        ref->named_type = named_type;
    }
        
    ref->locs.emplace_back(src_file.file_number, prev.span);
    return ref->named_type;
}
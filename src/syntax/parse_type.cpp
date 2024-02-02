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
    case TOK_STAR:
        next();
        return arena.New<PointerType>(parseTypeLabel());
    default:
        reject("expected type label");
        return nullptr;
    }
}
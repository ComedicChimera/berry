#include "token.hpp"

#include <unordered_map>

std::unordered_map<TokenKind, std::string> kindNames {
    { TOK_LET, "let" },
    { TOK_FUNC, "func" },
    { TOK_AS, "as" },
    { TOK_NULL, "null" },
    { TOK_I8, "i8" },
    { TOK_U8, "u8" },
    { TOK_I16, "i16" },
    { TOK_U16, "u16" },
    { TOK_I32, "i32" },
    { TOK_U32, "u32" },
    { TOK_I64, "i64" },
    { TOK_U64, "u64" },
    { TOK_F32, "f32" },
    { TOK_F64, "f64" },
    { TOK_BOOL, "bool" },
    { TOK_UNIT, "unit" },

    { TOK_PLUS, "+" },
    { TOK_MINUS, "-" },
    { TOK_STAR, "*" },
    { TOK_FSLASH, "/" },
    { TOK_MOD, "%" },
    { TOK_AMP, "&" },
    { TOK_PIPE, "|" },
    { TOK_CARRET, "^" },
    { TOK_ASSIGN, "=" },

    { TOK_LPAREN, "(" },
    { TOK_RPAREN, ")" },
    { TOK_LBRACKET, "[" },
    { TOK_RBRACKET, "]" },
    { TOK_LBRACE, "{" },
    { TOK_RBRACE, "}" },
    { TOK_COMMA, "," },
    { TOK_DOT, "." },
    { TOK_SEMI, ";" },
    { TOK_COLON, ":" },
    { TOK_ATSIGN, "@" },

    { TOK_IDENT, "identifier" },
    { TOK_INTLIT, "int literal" },
    { TOK_FLOATLIT, "float literal" },
    { TOK_BOOLLIT, "bool literal" },
    { TOK_STRLIT, "string literal" },
    { TOK_RUNELIT, "rune literal" },

    { TOK_EOF, "end of file" }
};

std::string_view tokKindToString(TokenKind kind) {
    auto it = kindNames.find(kind);
    Assert(it != kindNames.end(), "missing token kind to name mapping");
    return it->second;
}
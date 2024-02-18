#include "token.hpp"

#include <unordered_map>

std::string kindNames[TOKENS_COUNT] = {
    "let",
    "func",
    "if",
    "elif",
    "else",
    "while",
    "for",
    "do",
    "break",
    "continue",
    "return",
    "new",
    "as",
    "null",
    "i8",
    "u8",
    "i16",
    "u16",
    "i32",
    "u32",
    "i64",
    "u64",
    "f32",
    "f64",
    "bool",
    "unit",
    "+",
    "-",
    "*",
    "/",
    "%",
    "<<",
    ">>",
    "==",
    "!=",
    "<",
    ">",
    "<=",
    ">=",
    "&",
    "|",
    "^",
    "~",
    "!",
    "&&",
    "||",
    "=",
    "+=",
    "-=",
    "*=",
    "/=",
    "%=",
    "<<=",
    ">>=",
    "&=",
    "|=",
    "^=",
    "&&=",
    "||=",
    "++",
    "--",
    "(",
    ")",
    "[",
    "]",
    "{{",
    "}}",
    ",",
    ".",
    ";",
    ":",
    "@",
    "identifier",
    "int literal",
    "float literal",
    "bool literal",
    "string literal",
    "rune literal",
    "end of file"
};

std::string_view tokKindToString(TokenKind kind) {
    return kindNames[(int)kind];
}
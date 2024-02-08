#ifndef TOKEN_H_INC
#define TOKEN_H_INC

#include "base.hpp"

// TokenKind enumerates the different kinds of tokens.
enum TokenKind {
    TOK_LET,
    TOK_FUNC,

    TOK_IF,
    TOK_ELIF,
    TOK_ELSE,
    TOK_WHILE,
    TOK_FOR,

    TOK_BREAK,
    TOK_CONTINUE,
    TOK_RETURN,

    TOK_AS,
    TOK_NULL,

    TOK_I8,
    TOK_U8,
    TOK_I16,
    TOK_U16,
    TOK_I32,
    TOK_U32,
    TOK_I64,
    TOK_U64,
    TOK_F32,
    TOK_F64,
    TOK_BOOL,
    TOK_UNIT,

    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_FSLASH,
    TOK_MOD,

    TOK_SHL,
    TOK_SHR,

    TOK_EQ,
    TOK_NE,
    TOK_LT,
    TOK_GT,
    TOK_LE,
    TOK_GE,

    TOK_AMP,
    TOK_PIPE,
    TOK_CARRET,

    TOK_NOT,
    TOK_AND,
    TOK_OR,

    TOK_ASSIGN,
    TOK_PLUS_ASSIGN,
    TOK_MINUS_ASSIGN,
    TOK_STAR_ASSIGN,
    TOK_FSLASH_ASSIGN,
    TOK_MOD_ASSIGN,
    TOK_SHL_ASSIGN,
    TOK_SHR_ASSIGN,
    TOK_AMP_ASSIGN,
    TOK_PIPE_ASSIGN,
    TOK_CARRET_ASSIGN,
    TOK_AND_ASSIGN,
    TOK_OR_ASSIGN,

    TOK_INC,
    TOK_DEC,

    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_COMMA,
    TOK_DOT,
    TOK_SEMI,
    TOK_COLON,
    TOK_ATSIGN,

    TOK_IDENT,
    TOK_INTLIT,
    TOK_FLOATLIT,
    TOK_BOOLLIT,
    TOK_STRLIT,
    TOK_RUNELIT,

    TOK_EOF
};

// Token is a lexical token.
struct Token {
    // kind is the token's kind.
    TokenKind kind;

    // value is the owned text/value of the token.
    std::string value;

    // span is the token's location in source text.
    TextSpan span;
};

// tokKindToString converts kind to its string representation.
std::string_view tokKindToString(TokenKind kind);

#endif
#include "lexer.hpp"

#include <stdarg.h>
#include <ctype.h>
#include <unordered_map>

Lexer::Lexer(std::ifstream& file_, const SourceFile& src_file_)
: file(file_)
, src_file(src_file_)
, line(1)
, col(1)
, start_line(1)
, start_col(1)
, rlen(0)
{}

void Lexer::NextToken(Token& tok) {
    while (peek()) {
        switch (ahead) {
        case '\n':
        case '\t':
        case '\r':
        case ' ':
            skip();
            break;
        case '/':
            mark();
            read();

            if (peek()) {
                if (ahead == '/') {
                    skipLineComment();
                    continue;
                } else if (ahead == '*') {
                    skipBlockComment();
                    continue;
                }
            }

            makeToken(tok, TOK_FSLASH);
            return;
        case '\'':
            lexRuneLit(tok);
            return;
        case '"':
            lexStrLit(tok);
            return;
        case '+':
            lexSingle(tok, TOK_PLUS);
            return;
        case '-':
            lexSingle(tok, TOK_MINUS);
            return;
        case '*':
            lexSingle(tok, TOK_STAR);
            return;
        case '%':
            lexSingle(tok, TOK_MOD);
            return;
        case '&':
            lexSingle(tok, TOK_AMP);
            return;
        case '|':
            lexSingle(tok, TOK_PIPE);
            return;
        case '^':
            lexSingle(tok, TOK_CARRET);
            return;
        case '=':
            lexSingle(tok, TOK_ASSIGN);
            return;
        case '(':
            lexSingle(tok, TOK_LPAREN);
            return;
        case ')':
            lexSingle(tok, TOK_RPAREN);
            return;
        case '[':
            lexSingle(tok, TOK_LBRACKET);
            return;
        case ']':
            lexSingle(tok, TOK_RBRACKET);
            return;
        case '{':
            lexSingle(tok, TOK_LBRACE);
            return;
        case '}':
            lexSingle(tok, TOK_RBRACE);
            return;
        case ',':
            lexSingle(tok, TOK_COMMA);
            return;
        case '.':
            lexSingle(tok, TOK_DOT);
            return;
        case ';':
            lexSingle(tok, TOK_SEMI);
            return;
        case ':':
            lexSingle(tok, TOK_COLON);
            return;
        case '@':
            lexSingle(tok, TOK_ATSIGN);
            return;
        default:
            if (isdigit(ahead)) {
                lexNumberLit(tok);
                return;
            } else if (isalpha(ahead) || ahead == '_') {
                lexKeywordOrIdent(tok);
                return;
            } else {
                fatal("unknown rune: U+{:X}\n", ahead);
            }
        }
    }

    tok.kind = TOK_EOF;
}

/* -------------------------------------------------------------------------- */

static std::unordered_map<std::string, TokenKind> keyword_patterns {
    { "let", TOK_LET },
    { "func", TOK_FUNC },
    { "as", TOK_AS },
    { "null", TOK_NULL },
    { "i8", TOK_I8 },
    { "u8", TOK_U8 },
    { "i16", TOK_I16 },
    { "u16", TOK_U16 },
    { "i32", TOK_I32 },
    { "u32", TOK_U32 },
    { "i64", TOK_I64 },
    { "u64", TOK_U64 },
    { "f32", TOK_F32 },
    { "f64", TOK_F64 },
    { "bool", TOK_BOOL },
    { "unit", TOK_UNIT },
    { "true", TOK_BOOLLIT },
    { "false", TOK_BOOLLIT }
};

void Lexer::lexKeywordOrIdent(Token& tok) {
    mark();
    read();

    while (peek() && (isalnum(ahead) || ahead == '_')) {
        read();
    }

    auto it = keyword_patterns.find(tok_buff);
    if (it != keyword_patterns.end()) {
        makeToken(tok, it->second);
    } else {
        makeToken(tok, TOK_IDENT);
    }
}

/* -------------------------------------------------------------------------- */

static bool isHexDigit(rune r) {
    return isdigit(r) || ('A' <= r && r < 'G') || ('a' <= r && r < 'g');
}

static bool isDecDigit(rune r) {
    return isdigit(r);
}

void Lexer::lexNumberLit(Token& tok) {
    mark();
    read();

    int base = 10;
    if (peek()) {
        switch (ahead) {
        case 'b':
            read();
            base = 2;
            break;
        case 'o':
            read();
            base = 8;
            break;
        case 'x':
            read();
            base = 16;
            break;
        default:
            break;
        }
    }

    bool is_float = false;
    switch (base) {
    case 2:
        while (peek()) {
            if (ahead == '0' || ahead == '1') {
                read();
            } else if (ahead == '_') {
                skip();
            } else {
                break;
            }
        }
        break;
    case 8:
        while (peek()) {
            if ('0' <= ahead && ahead < '9') {
                read();
            } else if (ahead == '_') {
                skip();
            } else {
                break;
            }
        }
        break;
    case 10:
        is_float = readFloatOrIntLit(tok, isDecDigit, 'e', false);
        break;
    case 16:
        is_float = readFloatOrIntLit(tok, isHexDigit, 'p', true);
        break;
    }

    if (is_float) {
        makeToken(tok, TOK_FLOATLIT);
    } else {
        makeToken(tok, TOK_INTLIT);
    }
}

bool Lexer::readFloatOrIntLit(Token& tok, DigitCheckFunc f_is_digit, char exp_char_lower, bool expect_digit) {
    bool is_float = false, has_exp = false;
    
    char exp_char_upper = toupper(exp_char_lower);

    while (peek()) {
        if (ahead == '.') {
            if (has_exp) {
                fatal("decimal point cannot occur in exponent");
            } else if (is_float) {
                fatal("multiple decimal points in float literal");
            }

            is_float = true;
            expect_digit = true;
            read();
        } else if (ahead == '_') {
            skip();
            continue;
        } else if (f_is_digit(ahead)) {
            expect_digit = false;
            read();
        } else if (ahead == exp_char_lower || ahead == exp_char_upper) {
            if (has_exp) {
                fatal("multiple exponents in float literal");
            } 

            has_exp = true;
            is_float = true;
            expect_digit = true;
            read();

            if (peek() && ahead == '-') {
                read();
            }
        } else {
            break;
        }
    }

    if (expect_digit) {
        fatal("expected digit to end float literal");
    }

    return is_float;
}

/* -------------------------------------------------------------------------- */

void Lexer::lexSingle(Token& tok, TokenKind kind) {
    mark();
    read();
    makeToken(tok, kind);
}

/* -------------------------------------------------------------------------- */

void Lexer::lexStrLit(Token& tok) {
    mark();
    skip();

    while (peek()) {
        if (ahead == '\n') {
            break;
        } else if (ahead == '\"') {
            skip();
            makeToken(tok, TOK_STRLIT);
            return;
        } else if (ahead == '\\') {
            readEscapeSeq();
        } else {
            read();
        }
    }

    fatal("unclosed string literal");
}

void Lexer::lexRuneLit(Token& tok) {
    mark();
    skip();

    if (!peek()) {
        fatal("unclosed rune literal");
    } else if (ahead == '\'') {
        fatal("empty rune literal");
    } else if (ahead == '\n') {
        fatal("rune literal can't contain newline");
    } else if (ahead == '\\') {
        readEscapeSeq();
    } else {
        read();
    }

    if (!peek()) {
        fatal("unclosed rune literal");
    } else if (ahead != '\'') {
        fatal("rune literal is too long");
    }

    skip();

    makeToken(tok, TOK_RUNELIT);
}

void Lexer::readEscapeSeq() {
    read();

    if (!peek()) {
        fatal("expected escape sequence");
    }

    switch (ahead) {
    case 'a':
    case 'b':
    case 'f':
    case 'n':
    case 'r':
    case 't':
    case 'v':
    case '0':
    case '\'':
    case '"':
    case '\\':
        read();
        break;
    default:
        skip();
        fatal("invalid escape sequence");
    }
}

/* -------------------------------------------------------------------------- */

void Lexer::skipLineComment() {
    tok_buff.clear();
    skip();

    while (peek() && ahead != '\n') {
        skip();
    }
}

void Lexer::skipBlockComment() {
    tok_buff.clear();
    skip();

    while (peek()) {
        skip();

        if (ahead == '*' && peek() && ahead == '/') {
            skip();
            return;
        }
    }
}

/* -------------------------------------------------------------------------- */

void Lexer::mark() {
    start_line = line;
    start_col = col;
}

void Lexer::makeToken(Token& tok, TokenKind kind) {
    tok.kind = kind;
    tok.value = std::move(tok_buff);
    tok.span = getSpan();
}

/* -------------------------------------------------------------------------- */

rune Lexer::read() {
    rune r;
    if (rlen > 0) {
        r = ahead;
    } else {
        r = getRune();
        if (r == -1) {
            return -1;
        }
    }

    writeRune();
    updatePos(r);
    rlen = 0;

    return r;
}

rune Lexer::skip() {
    rune r;
    if (rlen > 0) {
        r = ahead;
    } else {
        r = getRune();
        if (r == -1) {
            return -1;
        }
    }

    updatePos(r);
    rlen = 0;

    return r;
}

bool Lexer::peek() {
    if (rlen > 0) {
        return true;
    } else {
        ahead = getRune();
        return ahead != -1;
    }
}

void Lexer::updatePos(rune r) {
    switch (r) {
    case '\n':
        line++;
        col = 1;
        break;
    case '\t':
        col += 4;
        break;
    default:
        col++;
        break;
    }
}

/* -------------------------------------------------------------------------- */

rune Lexer::getRune() {
    byte b1 = file.get();
    if (b1 == 0xff) {
        return -1;
    }
    
    rbuff[0] = b1;

    int n_bytes = 0;
    int32_t r;
    if ((b1 & 0x80) == 0) { // 0xxxxxxx
        rlen = 1;
        return b1;
    } else if ((b1 & 0xe0) == 0xc0) { // 110xxxxx
        n_bytes = 1;
        r = b1 & 0x1f;
    } else if ((b1 & 0xf0) == 0xe0) { // 1110xxxx
        n_bytes = 2;
        r = b1 & 0x0f;
    } else if ((b1 & 0xf8) == 0xf0) { // 11110xxx
        n_bytes = 3;
        r = b1 & 0x07;
    } else {
        fatal("malformed rune: invalid leading byte: {}", b1);
    }

    for (int i = 0; i < n_bytes; i++) {
        byte b = file.get();
        if (b == -1) {
            fatal("malformed rune: expected {} bytes; got EOF at {} bytes", n_bytes + 1, i + 1);
        }

        rbuff[i+1] = b;

        r <<= 6;
        r = r | (b & 0x3f);
    }

    rlen = n_bytes + 1;

    return r;
}

void Lexer::writeRune() {
    tok_buff.append(rbuff, rlen);
}

/* -------------------------------------------------------------------------- */

TextSpan Lexer::getSpan() {
    return {start_line, start_col, line, col};
}

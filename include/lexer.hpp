#ifndef LEXER_H_INC
#define LEXER_H_INC

#include <iostream>
#include <fstream>

#include "token.hpp"
#include "symbol.hpp"

// DigitCheckFunc returns whether r is a kind of digit (ex: isHexDigit).
// This type is used to make the number lexing code more reusable.
typedef bool (*DigitCheckFunc)(rune r);

// Lexer tokenizes a file into lexemes.
class Lexer {
    // file is the file stream being read.
    std::ifstream& file;

    // src_file is the Berry source file being lexed.
    const SourceFile& src_file;

    // tok_buff is the buffer used to build the current token.
    std::string tok_buff;

    // line and col indicate the lexer's current position in the file.
    size_t line, col;

    // start_line and start_col indicate the start of the current token.
    size_t start_line, start_col;

    // ahead is the lookahead rune (peeked but not read).
    rune ahead;

    // rbuff stores the UTF-8 bytes of the lookahead.
    char rbuff[4];

    // rlen is the number of bytes rbuff needed to represent the lookahead.
    int rlen;

public:
    // Creates a new lexer reading from file for src_file.
    Lexer(std::ifstream& file, const SourceFile& src_file);

    // NextToken reads the next token from the lexer into tok.
    void NextToken(Token &tok);

private:
    // lexKeywordOrIdent lexes a keyword or identifier.
    void lexKeywordOrIdent(Token &tok);

    /* ---------------------------------------------------------------------- */

    // lexNumberLit lexes a number literal.
    void lexNumberLit(Token &tok);

    // readFloatOrIntLit reads a number literal that can be either or a float or
    // an integer.  f_is_digit checks if a rune is a satisfactory digit,
    // exp_char_lower is the lowercase exponential character for floats (ex: e
    // in base 10, p in base 16), and expect_digit indicates whether a digit
    // must be read or if it is ok for no (additional) digits to be read in.
    bool readFloatOrIntLit(Token &tok, DigitCheckFunc f_is_digit, char exp_char_lower, bool expect_digit);

    /* ---------------------------------------------------------------------- */

    // lexSingle lexes a token of kind kind made up of a single character.
    void lexSingle(Token &tok, TokenKind kind);

    /* ---------------------------------------------------------------------- */

    // lexStrLit lexes a standard string literal.
    void lexStrLit(Token &tok);

    // lexRuneLit lexes a standard rune literal.
    void lexRuneLit(Token &tok);

    // readEscapeSeq reads an escape sequence (ex: \n, \0)
    void readEscapeSeq();

    /* ---------------------------------------------------------------------- */

    // skipLineComment skips a line comment.
    void skipLineComment();

    // skipBlockComment skips a block comment.
    void skipBlockComment();

    /* ---------------------------------------------------------------------- */

    // mark marks the lexer's current position as the start of the next token.
    void mark();

    // makeToken creates a new token stored in tok from the contents of tok_buff
    // of kind kind.  This clears tok_buff.
    void makeToken(Token &tok, TokenKind kind);

    /* ---------------------------------------------------------------------- */

    // updatePos updates the lexer's line and column based on r.
    void updatePos(rune r);

    // read moves the lexer forward one rune and writes it into tok_buff. The
    // read-in rune is returned.
    rune read();

    // skip moves the lexer forward one rune without writing said rune into
    // tok_buff.  The skipped rune is returned.
    rune skip();

    // peek reads the next rune into ahead without moving the lexer forward. It
    // returns false if an EOF is encountered.
    bool peek();

    /* ---------------------------------------------------------------------- */

    // getRune reads a UTF8 encoded rune from file.  The bytes of the rune are
    // stored in rbuff, and the number of bytes of the rune is stored in rlen.
    rune getRune();

    // writeRune writes the contents of rbuff into tok_buff without clearing
    // rbuff. Effectively, it moves the previously read in rune into tok_buff.
    void writeRune();

    /* ---------------------------------------------------------------------- */

    // fatal reports a compile error and throws a CompileError to abort lexing.
    template<typename... Args>
    inline void fatal(const std::string& msg, Args&&... args) {
        ReportCompileError(src_file.parent->name, src_file.display_path, getSpan(), msg, args...);
        throw CompileError{};
    }

    // getSpan computes a text span from the lexer's current position and the
    // saved start position of the current token (saved via mark).
    TextSpan getSpan();
};

#endif
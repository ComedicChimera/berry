#ifndef PARSER_H_INC
#define PARSER_H_INC

#include "ast.hpp"
#include "lexer.hpp"
#include "arena.hpp"

// Parser parses a Berry file into an AST and catches syntax errors.
class Parser {    
    // arena is the arena the parser should allocated types and symbols in.
    Arena& arena;

    // lexer is the parser's lexer for the file.
    Lexer lexer;

    // src_file is the Berry source file being parsed.  The AST definitions will
    // be added to src_file.defs as parsing occurs. 
    SourceFile& src_file;

    // tok is the token the parser is currently positioned over.
    Token tok;

    // prev is the previous token seen by the parser.
    Token prev;

public:
    // Creates a new parser reading from file for src_file.
    Parser(Arena& arena, std::ifstream& file, SourceFile& src_file)
    : arena(arena)
    , src_file(src_file)
    , lexer(file, src_file)
    {}
    
    // ParseFile runs the parser on the parser's file.
    void ParseFile();

private:
    void parseMetadata(MetadataMap &meta);
    void parseMetaTag(MetadataMap &meta);

    void parseDef(MetadataMap &&meta);
    void parseFuncDef(MetadataMap &&meta);
    void parseFuncParams(std::vector<Symbol *> &params);
    void parseGlobalVarDef(MetadataMap &&meta);

    /* ---------------------------------------------------------------------- */

    AstStmt* parseBlock();
    AstStmt* parseStmt();

    AstStmt* parseIfStmt();
    AstStmt* parseWhileLoop();
    AstStmt* parseDoWhileLoop();
    AstStmt* parseForLoop();
    AstStmt* maybeParseElse();

    AstStmt* parseLocalVarDef();
    AstStmt* parseExprAssignStmt();

    /* ---------------------------------------------------------------------- */

    AstExpr* parseExpr();
    AstExpr* parseBinaryOp(int pred_level);
    AstExpr* parseUnaryOp();
    AstExpr* parseAtomExpr();
    AstExpr* parseFuncCall(AstExpr* root);
    AstExpr* parseIndexOrSlice(AstExpr* root);
    AstExpr* parseAtom();
    AstExpr* parseArrayLit();

    /* ---------------------------------------------------------------------- */

    Type *parseTypeExt();
    Type *parseTypeLabel();

    /* ---------------------------------------------------------------------- */

    std::span<AstExpr*> parseExprList(TokenKind delim = TOK_COMMA);
    AstExpr* parseInitializer();
    std::vector<Token> parseIdentList(TokenKind delim = TOK_COMMA);

    /* ---------------------------------------------------------------------- */

    AstDef* allocDef(AstKind kind, const TextSpan& span, MetadataMap&& metadata);
    AstStmt* allocStmt(AstKind kind, const TextSpan& span);
    AstExpr* allocExpr(AstKind kind, const TextSpan& span);
    Type *allocType(TypeKind kind);

    /* ---------------------------------------------------------------------- */

    void defineGlobal(Symbol *symbol);

    /* ---------------------------------------------------------------------- */

    void next();
    bool has(TokenKind kind);
    void want(TokenKind kind);
    Token wantAndGet(TokenKind kind);

    /* ---------------------------------------------------------------------- */

    template<typename ...Args>
    inline void error(const TextSpan& span, const std::string& fmt, Args... args) {
        ReportCompileError(
            src_file.parent->name,
            src_file.display_path,
            span,
            fmt,
            args...
        );
    }

    template<typename ...Args>
    inline void fatal(const TextSpan& span, const std::string& fmt, Args... args) {
        error(span, fmt, args...);

        throw CompileError{};
    }

    template<typename ...Args>
    inline void reject(const std::string& fmt, Args... args) {
        std::string message_base { std::move(std::format(fmt, args...)) };
        message_base.append(" but got ");
        message_base.append(tokKindToString(tok.kind));

        fatal(
            tok.span, 
            message_base
        );
    }
};

bool ConvertUint(const std::string& int_str, uint64_t* value);

#endif
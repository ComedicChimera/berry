#ifndef PARSER_H_INC
#define PARSER_H_INC

#include "ast.hpp"
#include "lexer.hpp"
#include "arena.hpp"

using AttributeMap = std::unordered_map<std::string_view, Attribute>;

// Parser parses a Berry file into an AST and catches syntax errors.
class Parser {    
    // global_arena is the arena in which the parser should allocate global data
    // which will persist beyond the AST such as types and symbols.
    Arena& global_arena;

    // ast_arena is the arena the parser should allocate AST nodes in.
    Arena& ast_arena;

    // lexer is the parser's lexer for the file.
    Lexer lexer;

    // src_file is the Berry source file being parsed.  The AST definitions will
    // be added to src_file.defs as parsing occurs. 
    SourceFile& src_file;

    // tok is the token the parser is currently positioned over.
    Token tok;

    // prev is the previous token seen by the parser.
    Token prev;

    // allow_struct_lit_stack stores whether the parser is expecting struct
    // literal or if it should prioritze parsing blocks.  When enabled, the
    // parser will assume that any `{` it encounters during expression parsing
    // corresponds to a struct literal rather than the opening of a block.
    std::vector<bool> allow_struct_lit_stack;

    // directives_enabled is flag to indicate whether the parse should
    // automatically called parseDirective() when a directive is encountered or
    // whether it should simply proceed as usual.
    bool directives_enabled { true };

    // meta_if_depth counts how many if meta directives have been opened.
    int meta_if_depth { 0 };

public:
    // Creates a new parser reading from file for src_file.
    Parser(Arena& global_arena, Arena& ast_arena, std::ifstream& file, SourceFile& src_file)
    : global_arena(global_arena)
    , ast_arena(ast_arena)
    , src_file(src_file)
    , lexer(file, src_file)
    {}
    
    // ParseFile runs the parser on the parser's file.
    void ParseFile();

    // ParseModuleName returns the token corresponding to the file's module name
    // if it is present.  Otherwise, an empty token is returned.
    Token ParseModuleName();

private:
    void parseImportStmt();
    void parseModulePath();
    size_t findOrAddModuleDep(const std::vector<Token>& tok_mod_path);

    void parseAttrList(AttributeMap& attr_map);
    void parseAttribute(AttributeMap& attr_map);

    void parseDecl(AttributeMap&& attr_map, DeclFlags flags);
    AstNode* parseFuncOrMethodDecl(bool exported);
    AstNode* parseFactoryDecl(bool exported);
    AstNode* parseFuncSignature();
    void parseFuncParams(std::vector<AstFuncParam>& params);

    AstNode* parseGlobalVarDecl(bool exported);
    
    AstNode* parseStructDecl(bool exported);
    AstNode* parseAliasDecl(bool exported);
    AstNode* parseEnumDecl(bool exported);

    /* ---------------------------------------------------------------------- */

    AstNode* parseBlock();
    AstNode* parseStmt();

    AstNode* parseIfStmt();
    AstNode* parseWhileLoop();
    AstNode* parseDoWhileLoop();
    AstNode* parseForLoop();
    AstNode* maybeParseElse();
    AstNode* parseMatchStmt();

    AstNode* parseLocalVarDecl();
    AstNode* parseExprAssignStmt();

    /* ---------------------------------------------------------------------- */

    AstNode* parseCasePattern();
    AstNode* parsePattern();

    /* ---------------------------------------------------------------------- */

    AstNode* parseExpr();
    AstNode* parseBinaryOp(int pred_level);
    AstNode* parseUnaryOp();
    AstNode* parseAtomExpr();
    AstNode* parseFuncCall(AstNode* func);
    AstNode* parseIndexOrSlice(AstNode* root);
    AstNode* parseStructLit(AstNode* type);
    AstNode* parseAtom();
    AstNode* parseNewExpr();
    AstNode* parseArrayLit();
    AstNode* parseMacroCall();

    /* ---------------------------------------------------------------------- */

    AstNode* parseTypeExt();
    AstNode* parseTypeLabel();
    AstNode* parseStructTypeLabel();
    
    /* ---------------------------------------------------------------------- */

    std::span<AstNode*> parseExprList(TokenKind delim = TOK_COMMA);
    AstNode* parseInitializer();
    std::vector<Token> parseIdentList(TokenKind delim = TOK_COMMA);

    /* ---------------------------------------------------------------------- */

    void parseDirective();
    void parseMetaIfDirective();

    void skipMetaCondBody(bool should_run_else);

    std::string evaluateMetaExpr();
    std::string evaluateMetaAndExpr();
    std::string evaluateMetaEqExpr();
    std::string evaluateMetaUnaryExpr();
    std::string evaluateMetaValue();

    std::string_view lookupMetaVar(const std::string& name);

    /* ---------------------------------------------------------------------- */

    void pushAllowStructLit(bool allowed);
    void popAllowStructLit();
    bool shouldParseStructLit();

    /* ---------------------------------------------------------------------- */

    AstNode* allocNode(AstKind kind, const TextSpan& span);
    std::span<Attribute> moveAttrsToArena(AttributeMap&& meta_map);

    /* ---------------------------------------------------------------------- */

    void defineGlobal(Symbol *symbol);

    /* ---------------------------------------------------------------------- */

    void next();
    bool has(TokenKind kind);
    void want(TokenKind kind);
    Token wantAndGet(TokenKind kind);

    /* ---------------------------------------------------------------------- */

    template<typename ...Args>
    inline void error(const TextSpan& span, const std::string& fmt, Args&&... args) {
        ReportCompileError(
            src_file.display_path,
            span,
            fmt,
            args...
        );
    }

    template<typename ...Args>
    inline void fatal(const TextSpan& span, const std::string& fmt, Args&&... args) {
        error(span, fmt, args...);

        throw CompileError{};
    }

    template<typename ...Args>
    inline void reject(const std::string& fmt, Args&&... args) {
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
rune ConvertRuneLit(const std::string& rune_str); 

#endif
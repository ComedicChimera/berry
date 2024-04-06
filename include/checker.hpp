#ifndef CHECKER_H_INC
#define CHECKER_H_INC

#include "arena.hpp"
#include "ast.hpp"
#include "hir.hpp"

// Scope represents a local lexical scope.
typedef std::unordered_map<std::string_view, Symbol*> Scope;

// Checker performs semantic analysis on a source file.
class Checker {
    // arena is the arena used for allocation of symbols and types.
    Arena& arena;

    // mod is the module being checked.
    Module& mod;

    // src_file is the source file currently being checked.
    SourceFile* src_file { nullptr };

    // scope_stack keeps a stack of the enclosing local scopes with the one on
    // the top (end) being the current local scope.
    std::vector<Scope> scope_stack;

    // tctx is the checker's type context.
    TypeContext tctx;

    // null_spans maps untyped nulls to their corresponding spans.
    std::vector<std::pair<Type*, TextSpan>> null_spans;

    // enclosing_return_type is the return type of the function whose body is
    // being type checked. If the checker is running outside of a function body,
    // then this value is nullptr.
    Type* enclosing_return_type { nullptr };

    // loop_depth keeps track of how many enclosing loops there are.  This is
    // used for break and continue checking.
    int loop_depth { 0 };

    // fallthrough_stack keeps track of both the current match depth and whether
    // fallthough is enabled for a specific case.
    std::vector<bool> fallthrough_stack;

    // unsafe_depth keeps track of how many enclosing unsafe blocks there are.
    int unsafe_depth { 0 };

    // core_dep is a pointer to the core module dependency.  This will non-null
    // in every module except the core module itself.
    Module::DepEntry* core_dep;

    // type_explore_table keeps track of which named types have been expanded to
    // check for infinite recursive types.  The entries in this table correspond
    // directly to graph colors in three-color DFS:
    // No entry = White
    // True entry = Grey
    // False entry = Black
    std::unordered_map<std::string_view, bool> type_explore_table;

    // InitNode is a node in the init_graph.
    struct InitNode {
        std::unordered_set<size_t> edges;
        GColor color { COLOR_WHITE };
    };

    // init_graph is used to determine global variable initialization ordering
    // and to check for initialization cycles.
    std::vector<InitNode> init_graph;

    // is_comptime_expr is a flag set by `checkExpr` to indicate whether the
    // given expression is comptime.  
    bool is_comptime_expr { false };

public:
    // Creates a new checker for src_file allocating in arena.
    Checker(Arena& arena, Module& mod);

    // CheckModule performs semantic analysis on the checker's module.
    void CheckModule();

private:
    void checkDecl(size_t decl_number, Module::Decl& decl);

    HirDecl* checkFuncDecl(size_t decl_number, AstNode* node);
    HirDecl* checkGlobalVarDecl(size_t decl_number, AstNode* node);

    Type* checkDeclTypeLabel(size_t decl_number, AstNode* type_label);

    bool resolveTypeLabel(size_t decl_number, AstNode* type_label);

    /* ---------------------------------------------------------------------- */

    std::pair<HirStmt*, bool> checkStmt(AstNode* stmt);
    std::pair<HirStmt*, bool> checkBlock(AstNode* node);
    std::pair<HirStmt*, bool> checkIf(AstNode* node);
    HirStmt* checkWhile(AstNode* node);
    HirStmt* checkFor(AstNode* node);
    std::pair<HirStmt*, bool> checkMatchStmt(AstNode* node);
    HirStmt* checkLocalVar(AstNode* node);
    HirStmt* checkAssign(AstNode *node);
    HirStmt* checkIncDec(AstNode *node);
    HirStmt* checkReturn(AstNode *node);

    /* ---------------------------------------------------------------------- */

    bool checkCasePattern(AstNode* node, Type* expect_type, std::unordered_set<size_t>* enum_usages);
    bool checkPattern(AstNode* node, Type* expect_type, std::unordered_set<size_t>* enum_usages);

    void declarePatternCaptures(AstNode *pattern);

    bool isEnumExhaustive(Type* expr_type, const std::unordered_set<size_t>& enum_usages);

    /* ---------------------------------------------------------------------- */

    void checkExpr(AstNode* expr, Type* infer_type = nullptr);
    void checkDeref(AstNode* node);
    void checkCall(AstNode* node);
    void checkIndex(AstNode* node);
    void checkSlice(AstNode* node);
    void checkSelector(AstNode* node, bool expect_type);

    void checkArrayLit(AstNode* node, Type* infer_type);
    void checkNewType(AstNode* node);
    void checkNewArray(AstNode* node);
    void checkNewStruct(AstNode* node);
    void checkStructLit(AstNode* node, Type* infer_type);

    /* ---------------------------------------------------------------------- */

    // mustEqual asserts that a and b are equal.
    void mustEqual(const TextSpan& span, Type* a, Type* b);

    // mustSubType asserts that sub is a subtype of super.  It returns whether 
    bool mustSubType(const TextSpan& span, Type* sub, Type* super);

    // mustCast asserts that src can be cast to dest.
    void mustCast(const TextSpan& span, Type* src, Type* dest);

    void mustIntType(const TextSpan &span, Type *type);

    Type* mustApplyBinaryOp(const TextSpan& span, HirOpKind op, Type* lhs_type, Type* rhs_type);
    Type* maybeApplyPtrArithOp(Type* lhs_type, Type* rhs_type);

    Type* mustApplyUnaryOp(const TextSpan &span, HirOpKind op, Type* operand_type);

    void mustBeAssignable(HirExpr* expr);

    // newUntyped creates a new untyped of kind kind.
    Type* newUntyped(UntypedKind kind);

    void finishExpr();

    /* ---------------------------------------------------------------------- */

    void declareLocal(Symbol* sym);
    void pushScope();
    void popScope();

    /* ---------------------------------------------------------------------- */

    HirDecl* allocDecl(HirKind kind, TextSpan span);

    /* ---------------------------------------------------------------------- */

    template<typename ...Args>
    inline void error(const TextSpan& span, const std::string& fmt, Args&&... args) {
        ReportCompileError(
            src_file->display_path,
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
};

#endif
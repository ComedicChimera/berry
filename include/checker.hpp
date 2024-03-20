#ifndef CHECKER_H_INC
#define CHECKER_H_INC

#include "arena.hpp"
#include "ast.hpp"

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

    // enclosing_return_type is the return type of the function whose body is
    // being type checked. If the checker is running outside of a function body,
    // then this value is nullptr.
    Type* enclosing_return_type { nullptr };

    // loop_depth keeps track of how many enclosing loops there are.  This is
    // used for break and continue checking.
    int loop_depth { 0 };

    // core_dep is a pointer to the core module dependency.  This will non-null
    // in every module except the core module itself.
    Module::Dependency* core_dep;

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
    void checkDef(AstDef* def);
    bool checkMetadata(const Metadata& metadata, AstKind meta_kind);
    void checkFuncDef(AstDef* node);

    struct InitCycle {
        std::vector<size_t> nodes;
        bool done { false };
    };

    void checkGlobalVar(AstDef* node);
    bool checkInitOrder(size_t def_number, InitCycle& cycle);

    struct TypeCycle {
        std::vector<Type*> nodes;
        bool done { false };
    };

    void checkStructDef(AstDef* node);
    void checkAliasDef(AstDef *node);
    bool checkForInfType(Type* type, TypeCycle& cycle);
    void fatalOnTypeCycle(const TextSpan& span, TypeCycle &cycle);

    /* ---------------------------------------------------------------------- */

    bool checkStmt(AstStmt* stmt);
    bool checkBlock(AstStmt* node);
    bool checkIf(AstStmt* node);
    void checkWhile(AstStmt* node);
    void checkFor(AstStmt* node);
    void checkLocalVar(AstStmt *node);
    void checkAssign(AstStmt *node);
    void checkIncDec(AstStmt *node);
    void checkReturn(AstStmt *node);

    /* ---------------------------------------------------------------------- */

    void checkExpr(AstExpr* expr, Type* infer_type = nullptr);
    void checkDeref(AstExpr* node);
    void checkCall(AstExpr* node);
    void checkIndex(AstExpr* node);
    void checkSlice(AstExpr* node);
    void checkField(AstExpr* node, bool expect_type);
    Module::Dependency* checkIdentOrGetImport(AstExpr* node, bool expect_type);

    void checkArray(AstExpr* node, Type* infer_type);
    void checkNewExpr(AstExpr* node);
    void checkStructLit(AstExpr* node, Type* infer_type);

    /* ---------------------------------------------------------------------- */

    // mustEqual asserts that a and b are equal.
    void mustEqual(const TextSpan& span, Type* a, Type* b);

    // mustSubType asserts that sub is a subtype of super.
    void mustSubType(const TextSpan& span, Type* sub, Type* super);

    // mustCast asserts that src can be cast to dest.
    void mustCast(const TextSpan& span, Type* src, Type* dest);

    void mustIntType(const TextSpan &span, Type *type);

    Type* mustApplyBinaryOp(const TextSpan& span, AstOpKind aop, Type* lhs_type, Type* rhs_type);

    Type* mustApplyUnaryOp(const TextSpan &span, AstOpKind aop, Type* operand_type);

    void mustBeAssignable(AstExpr* expr);

    // newUntyped creates a new untyped of kind kind.
    Type* newUntyped(UntypedKind kind);

    inline void finishExpr() { 
        tctx.InferAll(); 
        tctx.Clear();
    }

    /* ---------------------------------------------------------------------- */

    void declareLocal(Symbol* sym);
    void pushScope();
    void popScope();
    
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
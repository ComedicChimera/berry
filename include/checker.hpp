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

    // src_file is the source file being checked.
    SourceFile& src_file;

    // scope_stack keeps a stack of the enclosing local scopes with the one on
    // the top (end) being the current local scope.
    std::vector<Scope> scope_stack;

    // tctx is the checker's type context.
    TypeContext tctx;

    // enclosing_return_type is the return type of the function whose body is
    // being type checked. If the checker is running outside of a function body,
    // then this value is nullptr.
    Type* enclosing_return_type;

    // loop_depth keeps track of how many enclosing loops there are.  This is
    // used for break and continue checking.
    int loop_depth;

public:
    // Creates a new checker for src_file allocating in arena.
    Checker(Arena& arena, SourceFile& src_file)
    : arena(arena)
    , src_file(src_file)
    , enclosing_return_type(nullptr)
    , loop_depth(0)
    {}

    void CheckDef(AstDef* def);

private:
    void checkMetadata(AstDef* def);
    void checkFuncDef(AstDef *node);
    void checkGlobalVar(AstDef *node);

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

    void checkExpr(AstExpr* expr);
    void checkDeref(AstExpr* node);
    void checkCall(AstExpr* node);
    void checkIndex(AstExpr* node);
    void checkSlice(AstExpr* node);
    void checkField(AstExpr* node);
    void checkArray(AstExpr* node);
    void checkNewExpr(AstExpr *node);

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

    Symbol* lookup(std::string_view name, const TextSpan& span);
    void declareLocal(Symbol* sym);
    void pushScope();
    void popScope();
    
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
};

#endif
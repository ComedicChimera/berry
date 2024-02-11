#ifndef CHECKER_H_INC
#define CHECKER_H_INC

#include "arena.hpp"
#include "visitor.hpp"

// Scope represents a local lexical scope.
typedef std::unordered_map<std::string_view, Symbol*> Scope;

// Checker performs semantic analysis on a source file.
class Checker : public Visitor {
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

    // Visitor methods
    void Visit(AstFuncDef& node) override;
    void Visit(AstLocalVarDef& node) override;
    void Visit(AstGlobalVarDef& node) override;
    void Visit(AstBlock& node) override;
    void Visit(AstCast& node) override;
    void Visit(AstBinaryOp& node) override;
    void Visit(AstUnaryOp& node) override;
    void Visit(AstAddrOf& node) override;
    void Visit(AstDeref& node) override;
    void Visit(AstCall& node) override;
    void Visit(AstIdent& node) override;
    void Visit(AstIntLit& node) override;
    void Visit(AstFloatLit& node) override;
    void Visit(AstBoolLit& node) override;
    void Visit(AstNullLit& node) override;
    void Visit(AstCondBranch& node) override;
    void Visit(AstIfTree& node) override;
    void Visit(AstWhileLoop& node) override;
    void Visit(AstForLoop& node) override;
    void Visit(AstIncDec& node) override;
    void Visit(AstAssign& node) override;
    void Visit(AstReturn& node) override;
    void Visit(AstBreak& node) override;
    void Visit(AstContinue& node) override;
    void Visit(AstIndex& node) override;
    void Visit(AstSlice& node) override;
    void Visit(AstArrayLit& node) override;
    void Visit(AstStringLit& node) override;
    void Visit(AstFieldAccess& node) override;

private:
    void checkFuncMetadata(AstFuncDef& fd);

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

    // newUntyped creates a new untyped of kind kind.
    Untyped* newUntyped(UntypedKind kind);

    inline void finishExpr() { 
        tctx.InferAll(); 
        tctx.Clear();
    }

    /* ---------------------------------------------------------------------- */

    Symbol* lookup(const std::string& name, const TextSpan& span);
    void declareLocal(Symbol* sym);
    void pushScope();
    void popScope();
    
    /* ---------------------------------------------------------------------- */

    template<typename ...Args>
    inline void error(const TextSpan& span, const std::string& fmt, Args&&... args) {
        ReportCompileError(
            src_file.parent->name,
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
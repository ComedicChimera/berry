#ifndef CHECKER_H_INC
#define CHECKER_H_INC

#include "arena.hpp"
#include "visitor.hpp"

// Scope represents a local lexical scope.
typedef std::unordered_map<std::string_view, Symbol*> Scope;

// Checker is Berry's semantic analyzer.
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

public:
    // Creates a new checker for src_file allocating in arena.
    Checker(Arena& arena, SourceFile& src_file)
    : arena(arena)
    , src_file(src_file)
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

private:
    void checkFuncMetadata(AstFuncDef& fd);

    /* ---------------------------------------------------------------------- */

    // MustEqual asserts that a and b are equal.
    void MustEqual(const TextSpan& span, Type* a, Type* b);

    // MustSubType asserts that sub is a subtype of super.
    void MustSubType(const TextSpan& span, Type* sub, Type* super);

    // MustCast asserts that src can be cast to dest.
    void MustCast(const TextSpan& span, Type* src, Type* dest);

    // MustNumberType asserts that type is a number type.
    void MustNumberType(const TextSpan& span, Type* type);

    // MustIntType asserts that type is an integer type.
    void MustIntType(const TextSpan& span, Type* type);
    
    // NewUntyped creates a new untyped of kind kind.
    Untyped* NewUntyped(UntypedKind kind);

    inline void FinishExpr() { 
        tctx.InferAll(); 
        tctx.Clear();
    }

    /* ---------------------------------------------------------------------- */

    Symbol* Lookup(const std::string& name, const TextSpan& span);
    void DeclareLocal(Symbol* sym);
    void PushScope();
    void PopScope();
    
    /* ---------------------------------------------------------------------- */

    template<typename ...Args>
    inline void Error(const TextSpan& span, const std::string& fmt, Args&&... args) {
        ReportCompileError(
            src_file.parent->name,
            src_file.display_path,
            span,
            fmt, 
            args...
        );
    }

    template<typename ...Args>
    inline void Fatal(const TextSpan& span, const std::string& fmt, Args&&... args) {
        Error(span, fmt, args...);

        throw CompileError{};
    }
};

#endif
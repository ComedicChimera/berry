#ifndef CHECKER_H_INC
#define CHECKER_H_INC

#include "symbol.hpp"
#include "arena.hpp"

typedef std::unordered_map<std::string_view, Symbol*> Scope;

class Checker {
    SourceFile& src_file;
    std::vector<Scope> scope_stack;
    TypeContext tctx;

public:
    Arena& arena;

    Checker(Arena& arena, SourceFile& src_file)
    : arena(arena)
    , src_file(src_file)
    {}

    /* ---------------------------------------------------------------------- */

    void MustEqual(const TextSpan& span, Type* a, Type* b);
    void MustSubType(const TextSpan& span, Type* sub, Type* super);
    void MustCast(const TextSpan& span, Type* src, Type* dest);

    void MustNumberType(const TextSpan& span, Type* type);
    void MustIntType(const TextSpan& span, Type* type);
    
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
    inline void ReportError(const TextSpan& span, const std::string& fmt, Args&&... args) {
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
        ReportError(span, fmt, args...);

        throw CompileError{};
    }
};

#endif
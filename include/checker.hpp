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

    // core_dep is a pointer to the core module dependency.  This will non-null
    // in every module except the core module itself.
    Module::DepEntry* core_dep;

    // tctx is the checker's type context.
    TypeContext tctx;

    // comptime_depth keeps track of the level of nested comptime expansion.
    int comptime_depth { 0 };

    // decl_number_stack keeps track of the current declaration being compiled.
    // It is used for tracing cycles during recursive declaration checking.
    std::vector<size_t> decl_number_stack;

    // curr_decl_number stores the number of the current decl being compiled.
    size_t curr_decl_number { 0 };

    // expr_is_comptime indicates if an expression which is not declared
    // comptime can be comptime.  This is used to promote global variable
    // initializers to constant values.
    bool expr_is_comptime { false };

    /* ---------------------------------------------------------------------- */

    // scope_stack keeps a stack of the enclosing local scopes with the one on
    // the top (end) being the current local scope.
    std::vector<Scope> scope_stack;


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

public:
    // Creates a new checker for src_file allocating in arena.
    Checker(Arena& arena, Module& mod);

    // CheckModule performs semantic analysis on the checker's module.
    void CheckModule();

private:
    void checkDecl(Decl* decl);

    HirDecl* checkFuncDecl(AstNode* node);
    HirDecl* checkGlobalVar(AstNode* node);
    HirDecl* checkGlobalConst(AstNode* node);
    HirDecl* checkTypeDef(AstNode* node);
    
    void checkFuncAttrs(Decl* decl);
    void checkGlobalVarAttrs(Decl* decl);

    Type* checkTypeLabel(AstNode* node, bool should_expand);

    /* ---------------------------------------------------------------------- */

    uint64_t checkComptimeSize(AstNode* expr);
    ConstValue* checkComptime(AstNode* expr, Type* infer_type);

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

    HirExpr* checkExpr(AstNode* node, Type* infer_type = nullptr);



    HirExpr* checkNewStruct(AstNode* node, Type* infer_type);
    HirExpr* checkArrayLit(AstNode* node, Type* infer_type);
    HirExpr* checkStructLit(AstNode* node, Type* infer_type);
    std::vector<HirFieldInit> checkFieldInits(std::span<AstNode*> afield_inits, Type* struct_type, Type* display_type);
    HirExpr* checkIdent(AstNode* node);
    

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

    HirExpr* createImplicitCast(HirExpr* src, Type* dest_type);

    /* ---------------------------------------------------------------------- */

    std::pair<Symbol*, Module::DepEntry*> mustLookup(std::string_view name, const TextSpan& span);
    Symbol* findSymbolInDep(Module::DepEntry& dep, std::string_view name);
    
    void declareLocal(Symbol* sym);
    void pushScope();
    void popScope();

    /* ---------------------------------------------------------------------- */

    HirDecl* allocDecl(HirKind kind, const TextSpan& span);
    HirStmt* allocStmt(HirKind kind, const TextSpan& span);
    HirExpr* allocExpr(HirKind kind, const TextSpan& span);

    inline Type* allocType(TypeKind kind) { return AllocType(arena, kind); }

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
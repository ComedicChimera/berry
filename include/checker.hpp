#ifndef CHECKER_H_INC
#define CHECKER_H_INC

#include "arena.hpp"
#include "ast.hpp"
#include "hir.hpp"

// Scope represents a local lexical scope.
typedef std::unordered_map<std::string_view, Symbol*> Scope;

// NullSpan represents an occurrence of a null literal in an expression.
struct NullSpan {
    Type* untyped;
    TextSpan span;
};

// Checker performs semantic analysis on a source file.
class Checker {
    // arena is the arena used for allocation of symbols and types.
    Arena& arena;

    // mod is the module being checked.
    Module& mod;

    // core_dep is a pointer to the core module dependency.  This will non-null
    // in every module except the core module itself.
    Module::DepEntry* core_dep;

    /* ------------------------ Declaration Ordering ------------------------ */

    // src_file stores the current source file being checked.
    SourceFile* src_file;
    
    // sorted_decls is a temporary vector used to hold declarations as they
    // are sorted into the correct order.  This will replace the module's decls
    // vector once sorting is complete.
    std::vector<Decl*> sorted_decls;

    // n_sorted counts the number of declarations which have already been
    // sorted. It is used as a cursor to determine where to insert the next
    // declaration in the sorted_decls array.
    size_t n_sorted { 0 };

    // first_pass is used to indicate what phase of checking is in progress.
    bool first_pass { true };

    // curr_decl_num stores the number of the current declaration being checked.
    size_t curr_decl_num;

    // decl_num_stack keeps a record of saved decl numbers during recursive
    // expansion of types and constants.
    std::vector<size_t> decl_num_stack;

    // init_table keeps track of dependences between global variables (through
    // functions) to perform init order checking.
    std::vector<std::unordered_set<size_t>> init_graph;

    /* ---------------- Local Variables and Scoped Quantities --------------- */

     // scope_stack keeps a stack of the enclosing local scopes with the one on
    // the top (end) being the current local scope.
    std::vector<Scope> scope_stack;

    // enclosing_return_type is the return type of the function whose body is
    // being type checked. If the checker is running outside of a function body,
    // then this value is nullptr.
    Type* enclosing_return_type { nullptr };

    // loop_depth keeps track of how many enclosing loops there are.  This is
    // used for break and continue checking.
    int loop_depth { 0 };

    // fallthru_stack keeps track of both the current match depth and whether
    // fallthough is enabled for a specific case.
    std::vector<bool> fallthru_stack;

    // unsafe_depth keeps track of how many enclosing unsafe blocks or
    // declarations there are.  If this is greater than 0, than unsafe
    // operations are allowed.  This has to be part of the local context since
    // an unsafe comptime could be expand a non-unsafe comptime and vice-versa.
    int unsafe_depth { 0 };

    // comptime_depth keeps track of the level of nested comptime expansion.
    int comptime_depth { 0 };

    /* -------------------------- Expression State -------------------------- */

    // tctx is the checker's type context.
    TypeContext tctx;
        
    // null_spans maps untyped nulls to their corresponding spans.
    std::vector<NullSpan> null_spans;

    // is_comptime_expr indicates if an expression which is not declared
    // comptime can be comptime.  This is used to promote global variable
    // initializers to constant values.
    bool is_comptime_expr { false };

    /* -------------------------- Pattern Matching -------------------------- */

    // PatternContext stores all state that is used for exhaustivity checking in
    // match statements and expression.  Anything that wants to call
    // checkCasePattern or checkPattern should push a pattern context before
    // doing so.
    struct PatternContext {
        // fallthru_used indicates whether a particular match case contains a
        // fallthrough statement.
        bool fallthru_used;

        // enum_usages keeps track of which enum variants have been matched between
        // different pattern cases.
        std::unordered_set<size_t> enum_usages;

        PatternContext() 
        : fallthru_used(false)
        {}
    };

    // pattern_ctx_stack is the stack of pattern contexts.
    std::vector<PatternContext> pattern_ctx_stack;

public:
    // Creates a new checker for src_file allocating in arena.
    Checker(Arena& arena, Module& mod);

    // CheckModule performs semantic analysis on the checker's module.
    void CheckModule();

private:
    bool addToInitOrder(Decl* decl);
    void reportCycle(Decl* decl);

    void checkDecl(Decl* decl);

    HirDecl* checkFuncDecl(AstNode* node);
    HirDecl* checkMethodDecl(AstNode* node, bool exported);
    HirDecl* checkFactoryDecl(AstNode* node, bool exported);
    Type* checkFuncSignature(AstNode* node, std::vector<Symbol*>& params);
    MethodTable& getMethodTable(Type* bind_type);

    HirDecl* checkGlobalVar(AstNode* node);
    HirDecl* checkGlobalConst(AstNode* node, bool unsafe);
    HirDecl* checkTypeDef(AstNode* node);

    HirStmt* checkFuncBody(AstNode* body, std::span<Symbol*> params, Type* return_type);
    void checkMethodBody(Decl* decl);
    void checkGlobalVarInit(Decl* decl);

    void checkFuncAttrs(Decl* decl);
    void checkMethodAttrs(Decl* decl);
    void checkFactoryAttrs(Decl* decl);
    void checkGlobalVarAttrs(Decl* decl);

    Type* checkTypeLabel(AstNode* node, bool should_expand);

    void pushDeclNum(size_t new_num);
    void popDeclNum();

    /* ---------------------------------------------------------------------- */

    uint64_t checkComptimeSize(AstNode* expr);

    ConstValue* evalComptime(HirExpr* expr);
    ConstValue* evalComptimeCast(HirExpr* node);
    ConstValue* evalComptimeBinaryOp(HirExpr* node);
    ConstValue* evalComptimeUnaryOp(HirExpr* node);
    ConstValue* evalComptimeStructLit(HirExpr* node);
    ConstValue* evalComptimeIndex(HirExpr* node);
    ConstValue* evalComptimeSlice(HirExpr* node);

    uint64_t evalComptimeIndexValue(HirExpr* node, uint64_t len);
    bool evalComptimeSizeValue(HirExpr* node, uint64_t* out_size);

    ConstValue* getComptimeNull(Type* type);
    ConstValue* allocComptime(ConstKind kind);
    void comptimeEvalError(const TextSpan& span, const std::string& message);

    /* ---------------------------------------------------------------------- */

    std::pair<HirStmt*, bool> checkStmt(AstNode* stmt);
    std::pair<HirStmt*, bool> checkBlock(AstNode* node);
    std::pair<HirStmt*, bool> checkIf(AstNode* node);
    HirStmt* checkWhile(AstNode* node);
    HirStmt* checkDoWhile(AstNode* node);
    HirStmt* checkFor(AstNode* node);
    std::pair<HirStmt*, bool> checkMatchStmt(AstNode* node);
    HirStmt* checkLocalVar(AstNode* node);
    HirStmt* checkLocalConst(AstNode* node);
    HirStmt* checkAssign(AstNode* node);
    HirStmt* checkIncDec(AstNode* node);
    HirStmt* checkReturn(AstNode* node);

    /* ---------------------------------------------------------------------- */

    std::pair<std::span<HirExpr*>, bool> checkCasePattern(AstNode* node, Type* expect_type);
    std::pair<HirExpr*, bool> checkPattern(AstNode* node, Type* expect_type);

    void declarePatternCaptures(HirExpr* pattern);
    bool isEnumExhaustive(Type* expr_type);

    PatternContext& getPatternCtx();
    void pushPatternCtx();
    void popPatternCtx();

    /* ---------------------------------------------------------------------- */

    HirExpr* checkExpr(AstNode* node, Type* infer_type = nullptr);

    HirExpr* checkAtomicCAS(AstNode* node);
    HirExpr* checkAtomicLoad(AstNode* node);
    HirExpr* checkAtomicStore(AstNode* node);
    HirExpr* checkAtomicPrimExpr(AstNode* node);
    HirMemoryOrder checkAtomicMemoryOrder(AstNode* node);

    HirExpr* checkCall(AstNode* node);
    HirExpr* checkFactoryCall(const TextSpan& span, Type* type, std::span<AstNode*> args);
    std::span<HirExpr*> checkArgs(const TextSpan& span, Type* func_type, std::span<AstNode*>& args);
    HirExpr* checkSelector(AstNode* node, Type* infer_type);
    HirExpr* checkField(HirExpr* root, std::string_view field_name, const TextSpan& span);
    std::pair<HirExpr*, Method*> checkFieldOrMethod(HirExpr* root, std::string_view field_name, const TextSpan& span);
    HirExpr* checkEnumLit(AstNode* node, Type* type);
    HirExpr* checkStaticGet(size_t dep_id, Symbol* imported_symbol, std::string_view mod_name, const TextSpan& span);
    HirExpr* checkNewArray(AstNode* node);
    HirExpr* checkNewStruct(AstNode* node, Type* infer_type);
    HirExpr* checkArrayLit(AstNode* node, Type* infer_type);
    HirExpr* checkStructLit(AstNode* node, Type* infer_type);
    std::vector<HirFieldInit> checkFieldInits(std::span<AstNode*> afield_inits, Type* struct_type, Type* display_type);
    HirExpr* checkValueSymbol(Symbol* symbol, const TextSpan& span);

    void markNonComptime(const TextSpan& span);
    void maybeExpandComptime(Symbol* symbol);

    /* ---------------------------------------------------------------------- */

    // mustEqual asserts that a and b are equal.
    void mustEqual(const TextSpan& span, Type* a, Type* b);

    // mustSubType asserts that sub is a subtype of super.  It returns whether 
    // an implicit subtype cast is required for conversion.
    bool mustSubType(const TextSpan& span, Type* sub, Type* super);

    // mustCast asserts that src can be cast to dest.
    void mustCast(const TextSpan& span, Type* src, Type* dest);

    void mustIntType(const TextSpan& span, Type *type);

    Type* mustApplyBinaryOp(const TextSpan& span, HirOpKind op, Type* lhs_type, Type* rhs_type);
    Type* maybeApplyPtrArithOp(Type* lhs_type, Type* rhs_type);
    Type* maybeApplyPtrCompareOp(Type* lhs_type, Type* rhs_type);

    Type* mustApplyUnaryOp(const TextSpan &span, HirOpKind op, Type* operand_type);

    // newUntyped creates a new untyped of kind kind.
    Type* newUntyped(UntypedKind kind);

    void finishExpr();

    HirExpr* createImplicitCast(HirExpr* src, Type* dest_type);

    // subtypeCast essentially combines mustSubType and createImplicitCast.
    HirExpr* subtypeCast(HirExpr* src, Type* dest_type);

    /* ---------------------------------------------------------------------- */

    std::pair<Symbol*, Module::DepEntry*> mustLookup(std::string_view name, const TextSpan& span);
    Symbol* findSymbolInDep(Module::DepEntry& dep, std::string_view name);
    Symbol* mustFindSymbolInDep(Module::DepEntry& dep, std::string_view name, const TextSpan& span);

    Method* tryLookupMethod(const TextSpan& span, Type* bind_type, std::string_view method_name);

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
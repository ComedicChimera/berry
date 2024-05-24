#include "checker.hpp"

HirDecl size_ref_decl;
HirStmt size_ref_stmt;
HirExpr size_ref_expr;

static size_t hir_variant_sizes[HIRS_COUNT] = {
    sizeof(size_ref_decl.ir_Func),
    sizeof(size_ref_decl.ir_GlobalVar),
    sizeof(size_ref_decl.ir_GlobalConst),
    sizeof(size_ref_decl.ir_TypeDef),
    sizeof(size_ref_decl.ir_TypeDef),
    sizeof(size_ref_decl.ir_TypeDef),
    sizeof(size_ref_decl.ir_Method),
    sizeof(size_ref_decl.ir_Factory),

    sizeof(size_ref_stmt.ir_Block),
    sizeof(size_ref_stmt.ir_If),
    sizeof(size_ref_stmt.ir_While),
    sizeof(size_ref_stmt.ir_While),
    sizeof(size_ref_stmt.ir_For),
    sizeof(size_ref_stmt.ir_Match),
    sizeof(size_ref_stmt.ir_Block),
    sizeof(size_ref_stmt.ir_LocalVar),
    sizeof(size_ref_stmt.ir_LocalConst),
    sizeof(size_ref_stmt.ir_Assign),
    sizeof(size_ref_stmt.ir_CpdAssign),
    sizeof(size_ref_stmt.ir_IncDec),
    sizeof(size_ref_stmt.ir_ExprStmt),
    sizeof(size_ref_stmt.ir_Return),
    0,
    0,
    0,

    sizeof(size_ref_expr.ir_TestMatch),
    sizeof(size_ref_expr.ir_Cast),
    sizeof(size_ref_expr.ir_Binop),
    sizeof(size_ref_expr.ir_Unop),
    sizeof(size_ref_expr.ir_Addr),
    sizeof(size_ref_expr.ir_Deref),
    sizeof(size_ref_expr.ir_Call),
    sizeof(size_ref_expr.ir_CallMethod),
    sizeof(size_ref_expr.ir_CallFactory),
    sizeof(size_ref_expr.ir_Index),
    sizeof(size_ref_expr.ir_Slice),
    sizeof(size_ref_expr.ir_Field),
    sizeof(size_ref_expr.ir_Field),
    sizeof(size_ref_expr.ir_New),
    sizeof(size_ref_expr.ir_NewArray),
    sizeof(size_ref_expr.ir_StructLit),
    sizeof(size_ref_expr.ir_ArrayLit),
    sizeof(size_ref_expr.ir_StructLit),
    sizeof(size_ref_expr.ir_EnumLit),
    sizeof(size_ref_expr.ir_StaticGet),
    sizeof(size_ref_expr.ir_UnsafeExpr),
    sizeof(size_ref_expr.ir_Ident),
    sizeof(size_ref_expr.ir_Num),
    sizeof(size_ref_expr.ir_Float),
    sizeof(size_ref_expr.ir_Bool),
    sizeof(size_ref_expr.ir_String),
    0,

    sizeof(size_ref_expr.ir_MacroType),
    sizeof(size_ref_expr.ir_MacroType),
    sizeof(size_ref_expr.ir_MacroAtomicCas),
    sizeof(size_ref_expr.ir_MacroAtomicLoad),
    sizeof(size_ref_expr.ir_MacroAtomicStore)
};

#define LARGEST_DECL_VARIANT_SIZE ((sizeof(size_ref_decl.ir_Method)))
#define LARGEST_STMT_VARIANT_SIZE ((sizeof(size_ref_stmt.ir_For)))
#define LARGEST_EXPR_VARIANT_SIZE ((sizeof(size_ref_expr.ir_MacroAtomicCas)))

HirDecl* Checker::allocDecl(HirKind kind, const TextSpan& span) {
    Assert(kind < HIR_BLOCK, "invalid kind for HIR decl");

    size_t variant_size = hir_variant_sizes[(size_t)kind];
    size_t alloc_size = sizeof(size_ref_decl) - LARGEST_DECL_VARIANT_SIZE + variant_size;

    auto* hdecl = (HirDecl*)arena.Alloc(alloc_size);
    hdecl->kind = kind;
    hdecl->span = span;

    return hdecl;
}

HirStmt* Checker::allocStmt(HirKind kind, const TextSpan& span) {
    Assert(HIR_BLOCK <= kind && kind < HIR_TEST_MATCH, "invalid kind for HIR stmt");

    size_t variant_size = hir_variant_sizes[(size_t)kind];
    size_t alloc_size = sizeof(size_ref_stmt) - LARGEST_STMT_VARIANT_SIZE + variant_size;

    auto* hstmt = (HirStmt*)arena.Alloc(alloc_size);
    hstmt->kind = kind;
    hstmt->span = span;

    return hstmt;
}

HirExpr* Checker::allocExpr(HirKind kind, const TextSpan& span) {
    Assert(HIR_TEST_MATCH <= kind, "invalid kind for HIR expr");

    size_t variant_size = hir_variant_sizes[(size_t)kind];
    size_t alloc_size = sizeof(size_ref_expr) - LARGEST_EXPR_VARIANT_SIZE + variant_size;

    auto* hexpr = (HirExpr*)arena.Alloc(alloc_size);
    hexpr->kind = kind;
    hexpr->span = span;
    hexpr->type = nullptr;
    hexpr->assignable = false;

    return hexpr;
}
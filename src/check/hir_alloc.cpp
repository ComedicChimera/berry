#include "checker.hpp"

HirDecl size_ref_decl;

static size_t hir_variant_sizes[HIRS_COUNT] = {
    sizeof(size_ref_decl.ir_Func),
    sizeof(size_ref_decl.ir_GlobalVar),
    sizeof(size_ref_decl.ir_GlobalConst),
    sizeof(size_ref_decl.ir_TypeDef),
    sizeof(size_ref_decl.ir_TypeDef),
    sizeof(size_ref_decl.ir_TypeDef),

    // TODO
};

#define LARGEST_DECL_VARIANT_SIZE ((sizeof(size_ref_decl.ir_Func)))

HirDecl* Checker::allocDecl(HirKind kind, TextSpan span) {
    Assert(kind < HIR_BLOCK, "invalid kind for HIR decl");

    size_t variant_size = hir_variant_sizes[(size_t)kind];
    size_t alloc_size = sizeof(size_ref_decl) - LARGEST_DECL_VARIANT_SIZE + variant_size;

    auto* hdecl = (HirDecl*)arena.Alloc(alloc_size);
    hdecl->span = span;

    return hdecl;
}
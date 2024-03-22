#include "parser.hpp"

static AstExpr size_ref_expr { };
static AstStmt size_ref_stmt { };
static AstDef size_ref_def { };

static size_t ast_variant_sizes[ASTS_COUNT] = {
    sizeof(size_ref_def.an_Func),
    sizeof(size_ref_def.an_GlVar),
    sizeof(size_ref_def.an_Struct),
    sizeof(size_ref_def.an_Alias),
    sizeof(size_ref_def.an_Enum),

    sizeof(size_ref_stmt.an_Block),
    sizeof(size_ref_stmt.an_If),
    sizeof(size_ref_stmt.an_While),
    sizeof(size_ref_stmt.an_For),
    sizeof(size_ref_stmt.an_LocalVar),
    sizeof(size_ref_stmt.an_Assign),
    sizeof(size_ref_stmt.an_IncDec),
    sizeof(size_ref_stmt.an_ExprStmt),
    sizeof(size_ref_stmt.an_Return),
    0,
    0,

    sizeof(size_ref_expr.an_Cast),
    sizeof(size_ref_expr.an_Binop),
    sizeof(size_ref_expr.an_Unop),
    sizeof(size_ref_expr.an_Addr),
    sizeof(size_ref_expr.an_Deref),
    sizeof(size_ref_expr.an_Call),
    sizeof(size_ref_expr.an_Index),
    sizeof(size_ref_expr.an_Slice),
    sizeof(size_ref_expr.an_Field),
    sizeof(size_ref_expr.an_Field),
    sizeof(size_ref_expr.an_Array),
    sizeof(size_ref_expr.an_New),
    sizeof(size_ref_expr.an_StructLitPos),
    sizeof(size_ref_expr.an_StructLitNamed),
    sizeof(size_ref_expr.an_StructLitPos),
    sizeof(size_ref_expr.an_StructLitNamed),
    0,
    sizeof(size_ref_expr.an_Field),
    0,
    sizeof(size_ref_expr.an_Ident),
    sizeof(size_ref_expr.an_Int),
    sizeof(size_ref_expr.an_Float),
    sizeof(size_ref_expr.an_Bool),
    0,
    sizeof(size_ref_expr.an_String),
};
#define LARGEST_DEF_VARIANT_SIZE sizeof(size_ref_def.an_Func)
#define LARGEST_STMT_VARIANT_SIZE sizeof(size_ref_stmt.an_For)
#define LARGEST_EXPR_VARIANT_SIZE sizeof(size_ref_expr.an_Field)

Metadata Parser::moveMetadataToArena(MetadataMap&& meta_map) {
    auto n_meta = meta_map.size();
    auto* metadata = (MetadataTag*)arena.Alloc(sizeof(MetadataTag) * n_meta);

    int n = 0;
    for (auto& pair : meta_map) {
        metadata[n] = pair.second;
        n++;
    }

    meta_map.clear();
    return Metadata(metadata, n_meta);
}

AstDef* Parser::allocDef(AstKind kind, const TextSpan& span, MetadataMap&& meta_map) {
    Assert(kind <= AST_ENUM, "invalid kind for allocDef");

    size_t var_size = ast_variant_sizes[(int)kind];
    size_t full_size = sizeof(AstDef) - LARGEST_DEF_VARIANT_SIZE + var_size;

    auto* def = (AstDef*)arena.Alloc(full_size);
    def->kind = kind;
    def->span = span;
    def->parent_file_number = src_file.file_number;
    def->metadata = moveMetadataToArena(std::move(meta_map));

    return def;
}

AstStmt* Parser::allocStmt(AstKind kind, const TextSpan& span) {
    Assert(AST_ENUM < kind && kind < AST_CAST, "invalid kind for allocStmt");

    size_t var_size = ast_variant_sizes[(int)kind];
    size_t full_size = sizeof(AstStmt) - LARGEST_STMT_VARIANT_SIZE + var_size;

    auto* stmt = (AstStmt*)arena.Alloc(full_size);
    stmt->kind = kind;
    stmt->span = span;
    return stmt;
}

AstExpr* Parser::allocExpr(AstKind kind, const TextSpan& span) {
    Assert(AST_CAST <= kind, "invalid kind for allocExpr");

    size_t var_size = ast_variant_sizes[(int)kind];
    size_t full_size = sizeof(AstExpr) - LARGEST_EXPR_VARIANT_SIZE + var_size;

    auto* expr = (AstExpr*)arena.Alloc(full_size);
    expr->kind = kind;
    expr->span = span;
    expr->type = nullptr;
    expr->immut = false;
    return expr;
}

Type* Parser::allocType(TypeKind kind) {
    return AllocType(arena, kind);
}


#include "parser.hpp"

static AstNode size_ref_node {};

static size_t ast_variant_sizes[ASTS_COUNT] = {
    sizeof(size_ref_node.an_Func),
    sizeof(size_ref_node.an_Var),
    sizeof(size_ref_node.an_Var),
    sizeof(size_ref_node.an_TypeDef),

    sizeof(size_ref_node.an_Block),
    sizeof(size_ref_node.an_If),
    sizeof(size_ref_node.an_While),
    sizeof(size_ref_node.an_While),
    sizeof(size_ref_node.an_For),
    sizeof(size_ref_node.an_Match),
    sizeof(size_ref_node.an_Block),
    sizeof(size_ref_node.an_Assign),
    sizeof(size_ref_node.an_IncDec),
    sizeof(size_ref_node.an_Return),
    0,
    0,
    0,

    sizeof(size_ref_node.an_TestMatch),
    sizeof(size_ref_node.an_Cast),
    sizeof(size_ref_node.an_Binop),
    sizeof(size_ref_node.an_Unop),
    sizeof(size_ref_node.an_Addr),
    sizeof(size_ref_node.an_Deref),
    sizeof(size_ref_node.an_Call),
    sizeof(size_ref_node.an_Index),
    sizeof(size_ref_node.an_Slice),
    sizeof(size_ref_node.an_Sel),
    sizeof(size_ref_node.an_StaticGet),
    sizeof(size_ref_node.an_ExprList),
    sizeof(size_ref_node.an_StructLit),
    sizeof(size_ref_node.an_New),
    sizeof(size_ref_node.an_NewArray),
    sizeof(size_ref_node.an_StructLit),
    sizeof(size_ref_node.an_Ident),
    sizeof(size_ref_node.an_Num),
    sizeof(size_ref_node.an_Float),
    sizeof(size_ref_node.an_Bool),
    sizeof(size_ref_node.an_Rune),
    sizeof(size_ref_node.an_String),
    0,

    sizeof(size_ref_node.an_Macro),
    sizeof(size_ref_node.an_Macro),
    sizeof(size_ref_node.an_Macro),

    sizeof(size_ref_node.an_TypePrim),
    sizeof(size_ref_node.an_TypeArray),
    sizeof(size_ref_node.an_TypeSlice),
    sizeof(size_ref_node.an_TypeFunc),
    sizeof(size_ref_node.an_TypeStruct),
    sizeof(size_ref_node.an_TypeEnum),
    
    sizeof(size_ref_node.an_ExprList),
    sizeof(size_ref_node.an_NamedInit),
    0
};

#define LARGEST_AST_VARIANT_SIZE sizeof(size_ref_node.an_For)

AstNode* Parser::allocNode(AstKind kind, const TextSpan& span) {
    size_t var_size = ast_variant_sizes[(int)kind];
    size_t full_size = sizeof(AstNode) - LARGEST_AST_VARIANT_SIZE + var_size;

    auto* node = (AstNode*)ast_arena.Alloc(full_size);
    node->kind = kind;
    node->span = span;
    return node;
}

std::span<Attribute> Parser::moveAttrsToArena(AttributeMap&& attr_map) {
    auto n_attrs = attr_map.size();
    auto* attr_data = (Attribute*)global_arena.Alloc(sizeof(Attribute) * n_attrs);

    int n = 0;
    for (auto& pair : attr_map) {
        attr_data[n] = pair.second;
        n++;
    }

    attr_map.clear();
    return std::span<Attribute>(attr_data, n_attrs);
}


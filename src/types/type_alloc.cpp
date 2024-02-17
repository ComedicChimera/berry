#include "types.hpp"

#include "arena.hpp"

static size_t type_variant_sizes[TYPES_COUNT] = {
    sizeof(prim_i8_type.ty_Int),    
    sizeof(prim_i8_type.ty_Float), 
    0,
    0,
    sizeof(prim_i8_type.ty_Ptr),    
    sizeof(prim_i8_type.ty_Func),   
    sizeof(prim_i8_type.ty_Array),  
    sizeof(prim_i8_type.ty_Untyp),  
};
#define LARGEST_TYPE_VARIANT_SIZE sizeof(prim_i8_type.ty_Func)

Type* AllocType(Arena& arena, TypeKind kind) {
    size_t var_size = type_variant_sizes[(int)kind];
    size_t full_size = sizeof(Type) - LARGEST_TYPE_VARIANT_SIZE + var_size;

    auto* type = (Type*)arena.Alloc(full_size);
    type->kind = kind;
    return type;
}

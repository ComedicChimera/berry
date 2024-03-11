#include "codegen.hpp"

ConstValue* CodeGenerator::evalComptime(AstExpr* node) {
    ConstValue* value;

    switch (node->kind) {
    case AST_CAST:
    case AST_BINOP:
    case AST_UNOP:
    case AST_INDEX:
    case AST_SLICE:
    case AST_FIELD: {
        auto* root_value = evalComptime(node->an_Field.root);

        if (root_value->kind == CONST_ARRAY) {
            Assert(node->an_Field.field_name == "_len", "access to array._ptr in comptime expr");

            // TODO: platform sized integers
            value = allocComptime(CONST_I64);
            value->v_i64 = (int64_t)root_value->v_comp.size();
        } else if (root_value->kind == CONST_STRING) {
            Assert(node->an_Field.field_name == "_len", "access to string._ptr in comptime expr");

            // TODO: platform sized integers
            value = allocComptime(CONST_I64);
            value->v_i64 = (int64_t)root_value->v_str.size();
        } else {
            Assert(root_value->kind == CONST_STRUCT, "invalid comptime field access");
            value = root_value->v_comp[node->an_Field.field_index];
        }
    } break;
    case AST_STATIC_GET: {
        auto* symbol = node->an_Field.imported_sym;
        Assert(symbol->flags & SYM_COMPTIME, "comptime eval with non-comptime symbol");

        if (symbol->flags & SYM_FUNC) {
            auto* value = allocComptime(CONST_FUNC);
            value->v_func = symbol;
        } else {
            auto* def = src_mod.deps[node->an_Field.root->an_Ident.dep_id].mod->defs[symbol->def_number];
            Assert(def->kind == AST_GLVAR && def->an_GlVar.const_value > CONST_VALUE_MARKER, "invalid comptime symbol");
            value = def->an_GlVar.const_value;
        }
    } break;
    case AST_NEW:
    case AST_ARRAY: {
        value = allocComptime(CONST_ARRAY);

        std::vector<ConstValue*> elems;
        for (auto* ast_elem : node->an_Array.elems) {
            elems.push_back(evalComptime(ast_elem));
        }

        value->v_comp = arena.MoveVec(std::move(elems));
    } break;
    case AST_STRUCT_LIT_POS:
    case AST_STRUCT_LIT_NAMED:  
    case AST_IDENT: {
        auto* symbol = node->an_Ident.symbol;
        Assert(symbol->flags & SYM_COMPTIME, "comptime eval with non-comptime symbol");

        if (symbol->flags & SYM_FUNC) {
            auto* value = allocComptime(CONST_FUNC);
            value->v_func = symbol;
        } else {
            auto* def = src_mod.defs[symbol->def_number];
            Assert(def->kind == AST_GLVAR && def->an_GlVar.const_value > CONST_VALUE_MARKER, "invalid comptime symbol");
            value = def->an_GlVar.const_value;
        }
    } break;
    case AST_INT: {
        auto& int_type = node->type->Inner()->ty_Int;

        switch (int_type.bit_size) {
        case 8:
            value = allocComptime(CONST_U8);
            value->v_u8 = (uint8_t)node->an_Int.value;
            break;
        case 16:
            value = allocComptime(CONST_U16);
            value->v_u16 = (uint16_t)node->an_Int.value;
            break;
        case 32:
            value = allocComptime(CONST_U32);
            value->v_u32 = (uint32_t)node->an_Int.value;
            break;
        case 64:
            value = allocComptime(CONST_U64);
            value->v_u64 = node->an_Int.value;
            break;
        }

        if (int_type.is_signed) {
            value->kind = (ConstKind)((int)value->kind - 1);
        }
    } break;
    case AST_FLOAT:
        if (node->type->Inner()->ty_Float.bit_size == 32) {
            value = allocComptime(CONST_F32);
            value->v_f32 = (float)node->an_Float.value;
        } else {
            value = allocComptime(CONST_F64);
            value->v_f64 = node->an_Float.value;
        }
        break;
    case AST_BOOL:
        value = allocComptime(CONST_BOOL);
        value->v_bool = node->an_Bool.value;
        break;
    case AST_STR:
        value = allocComptime(CONST_STRING);
        value->v_str = node->an_String.value;
        break;
    case AST_NULL:
        value = getComptimeNull(node->type);
        break;
    default:
        Panic("comptime evaluation not implemented for AST node");
        break;
    }

    return value;
}

ConstValue* CodeGenerator::getComptimeNull(Type* type) {
    type = type->Inner();

    if (type->kind == TYPE_NAMED) {
        type = type->ty_Named.type;
    }

    ConstValue* value;
    switch (type->kind) {
    case TYPE_INT: {
        auto& int_type = type->ty_Int;

        switch (int_type.bit_size) {
        case 8:
            value = allocComptime(CONST_I8);
            value->v_i8 = 0;
            break;
        case 16:
            value = allocComptime(CONST_I16);
            value->v_i16 = 0;
            break;
        case 32:
            value = allocComptime(CONST_I32);
            value->v_i32 = 0;
            break;
        case 64:
            value = allocComptime(CONST_I64);
            value->v_i64 = 0;
            break;
        }

        if (!int_type.is_signed) {
            value->kind = (ConstKind)((int)value->kind + 1);
        }
    } break;
    case TYPE_FLOAT:
        if (type->ty_Float.bit_size == 32) {
            value = allocComptime(CONST_F32);
            value->v_f32 = 0;
        } else {
            value = allocComptime(CONST_F64);
            value->v_f64 = 0;
        }
        break;
    case TYPE_BOOL:
        value = allocComptime(CONST_BOOL);
        value->v_bool = false;
        break;
    case TYPE_PTR:
        value = allocComptime(CONST_PTR);
        value->v_ptr = 0;
        break;
    case TYPE_FUNC:
        value = allocComptime(CONST_FUNC);
        value->v_func = nullptr;
        break;
    case TYPE_ARRAY:
        value = allocComptime(CONST_ARRAY);
        value->v_comp = {};
        break;
    case TYPE_STRING:
        value = allocComptime(CONST_STRING);
        value->v_str = "";
        break;
    case TYPE_STRUCT: {
        std::vector<ConstValue*> field_values;
        for (auto& field : type->ty_Struct.fields) {
            field_values.push_back(getComptimeNull(field.type));
        }

        value = allocComptime(CONST_STRUCT);
        value->v_comp = arena.MoveVec(std::move(field_values));
    } break;
    default:
        Panic("comptime null not implemented for type {}", (int)type->kind);
        break;
    }

    return value;
}

static ConstValue size_ref_const;

size_t const_variant_sizes[CONSTS_COUNT] = {
    sizeof(size_ref_const.v_i8),
    sizeof(size_ref_const.v_u8),
    sizeof(size_ref_const.v_i16),
    sizeof(size_ref_const.v_u16),
    sizeof(size_ref_const.v_i32),
    sizeof(size_ref_const.v_u32),
    sizeof(size_ref_const.v_i64),
    sizeof(size_ref_const.v_u64),
    sizeof(size_ref_const.v_f32),
    sizeof(size_ref_const.v_f64),
    sizeof(size_ref_const.v_bool),
    sizeof(size_ref_const.v_ptr),
    sizeof(size_ref_const.v_func),
    sizeof(size_ref_const.v_comp),
    sizeof(size_ref_const.v_str),
    sizeof(size_ref_const.v_comp)
};
#define LARGEST_CONST_VARIANT ((sizeof(size_ref_const.v_comp)))

ConstValue* CodeGenerator::allocComptime(ConstKind kind) {
    size_t alloc_size = sizeof(ConstValue) - LARGEST_CONST_VARIANT + const_variant_sizes[kind];

    auto* value = (ConstValue*)arena.Alloc(alloc_size);
    value->kind = kind;
    return value;
}

/* -------------------------------------------------------------------------- */

llvm::Constant* CodeGenerator::genComptime(ConstValue* value, AstAllocMode alloc_mode) {

}

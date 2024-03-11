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
            value->v_i64 = (int64_t)root_value->v_array.elems.size();
        } else if (root_value->kind == CONST_STRING) {
            Assert(node->an_Field.field_name == "_len", "access to string._ptr in comptime expr");

            // TODO: platform sized integers
            value = allocComptime(CONST_I64);
            value->v_i64 = (int64_t)root_value->v_str.value.size();
        } else {
            Assert(root_value->kind == CONST_STRUCT, "invalid comptime field access");
            value = root_value->v_struct.fields[node->an_Field.field_index];
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

        value->v_array.elems = arena.MoveVec(std::move(elems));
        value->v_array.elem_type = node->type->Inner()->ty_Array.elem_type->Inner();
        value->v_array.alloc_loc = nullptr;
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
        value->v_str.value = node->an_String.value;
        value->v_str.alloc_loc = nullptr;
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

    auto* named_type = type;
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
        value->v_array.elems = {};
        value->v_array.elem_type = type->ty_Array.elem_type->Inner();
        value->v_array.alloc_loc = nullptr;
        break;
    case TYPE_STRING:
        value = allocComptime(CONST_STRING);
        value->v_str.value = "";
        value->v_str.alloc_loc = nullptr;
        break;
    case TYPE_STRUCT: {
        std::vector<ConstValue*> field_values;
        for (auto& field : type->ty_Struct.fields) {
            field_values.push_back(getComptimeNull(field.type));
        }

        value = allocComptime(CONST_STRUCT);
        value->v_struct.fields = arena.MoveVec(std::move(field_values));
        value->v_struct.struct_type = named_type;
        value->v_struct.alloc_loc = nullptr;
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
    sizeof(size_ref_const.v_array),
    sizeof(size_ref_const.v_zarr),
    sizeof(size_ref_const.v_str),
    sizeof(size_ref_const.v_struct)
};
#define LARGEST_CONST_VARIANT ((sizeof(size_ref_const.v_struct)))

ConstValue* CodeGenerator::allocComptime(ConstKind kind) {
    size_t alloc_size = sizeof(ConstValue) - LARGEST_CONST_VARIANT + const_variant_sizes[kind];

    auto* value = (ConstValue*)arena.Alloc(alloc_size);
    value->kind = kind;
    return value;
}

/* -------------------------------------------------------------------------- */

llvm::Constant* CodeGenerator::genComptime(ConstValue* value, bool inside) {
    switch (value->kind) {
    case CONST_I8: return makeLLVMIntLit(&prim_i8_type, value->v_i8);
    case CONST_U8: return makeLLVMIntLit(&prim_u8_type, value->v_u8);
    case CONST_I16: return makeLLVMIntLit(&prim_i16_type, value->v_i16);
    case CONST_U16: return makeLLVMIntLit(&prim_u16_type, value->v_u16);
    case CONST_I32: return makeLLVMIntLit(&prim_i32_type, value->v_i32);
    case CONST_U32: return makeLLVMIntLit(&prim_u32_type, value->v_u32);
    case CONST_I64: return makeLLVMIntLit(&prim_i64_type, value->v_i64);
    case CONST_U64: return makeLLVMIntLit(&prim_u64_type, value->v_u64);
    case CONST_F32: return makeLLVMFloatLit(&prim_f32_type, value->v_f32);
    case CONST_F64: return makeLLVMFloatLit(&prim_f64_type, value->v_f64);
    case CONST_BOOL: return makeLLVMIntLit(&prim_bool_type, value->v_bool);
    case CONST_PTR: 
        if (value->v_ptr == 0) {
            return llvm::ConstantPointerNull::get(llvm::PointerType::get(ctx, 0));
        } else {
            return llvm::ConstantExpr::getIntToPtr(
                // TODO: platform integer sizes
                makeLLVMIntLit(&prim_i64_type, value->v_ptr),
                llvm::PointerType::get(ctx, 0)
            );
        }
    case CONST_FUNC: {
        auto* sym = value->v_func;

        if (sym->parent_id == src_mod.id) {
            return llvm::dyn_cast<llvm::Constant>(sym->llvm_value);
        } else {
            size_t dep_id = 0;
            for (auto& dep : src_mod.deps) {
                if (dep.mod->id == sym->parent_id)
                    break;

                dep_id++;
            }

            return llvm::dyn_cast<llvm::Constant>(loaded_imports[dep_id][sym->def_number]);
        }
    } break;
    case CONST_ARRAY:
    case CONST_ZERO_ARRAY:
        return genComptimeArray(value);
    case CONST_STRING:
    case CONST_STRUCT:
    default:
        Panic("unimplemented comptime value");
        break;
    }
}

static size_t const_id_counter = 0;

llvm::Constant* CodeGenerator::genComptimeArray(ConstValue* value) {
    llvm::Constant* alloc_loc;
    size_t num_elems;
    llvm::ArrayType* ll_arr_data_type;

    if (value->kind == CONST_ARRAY) {
        alloc_loc = value->v_array.alloc_loc;

        num_elems = value->v_array.elems.size();
        ll_arr_data_type = llvm::ArrayType::get(
            genType(value->v_array.elem_type, true), 
            num_elems
        );
    } else {
        alloc_loc = value->v_zarr.alloc_loc;

        num_elems = value->v_zarr.num_elems;
        ll_arr_data_type = llvm::ArrayType::get(
            genType(value->v_zarr.elem_type, true), 
            num_elems
        );
    }

    llvm::Constant* gv;
    if (alloc_loc == nullptr) {

        llvm::Constant* ll_array;
        if (value->kind == CONST_ARRAY) {
            std::vector<llvm::Constant*> ll_elems;
            for (auto* elem : value->v_array.elems) {
                ll_elems.push_back(genComptime(elem, true));
            }

            ll_array = llvm::ConstantArray::get(ll_arr_data_type, ll_elems);
        } else {
            ll_array = getNullValue(ll_arr_data_type);
        }

        gv = new llvm::GlobalVariable(
            mod, 
            ll_arr_data_type, 
            true, 
            llvm::GlobalValue::ExternalLinkage, 
            ll_array, 
            std::format("__$const{}", const_id_counter++)
        );
    } else {
        gv = mod.getGlobalVariable(alloc_loc->getName());

        if (gv == nullptr) {
            gv = new llvm::GlobalVariable(
                mod, 
                ll_arr_data_type, 
                true, 
                llvm::GlobalValue::ExternalLinkage, 
                nullptr, 
                alloc_loc->getName()
            );
        }
    }

    return llvm::ConstantStruct::get(ll_array_type, { gv, getPlatformIntConst(num_elems) });
}

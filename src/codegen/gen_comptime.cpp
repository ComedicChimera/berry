#include "codegen.hpp"

llvm::Constant* CodeGenerator::genComptime(ConstValue* value, ComptimeGenFlags flags, Type* expect_type) {
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
                makeLLVMIntLit(platform_uint_type, value->v_ptr),
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

            return llvm::dyn_cast<llvm::Constant>(loaded_imports[dep_id][sym->decl_number]);
        }
    } break;
    case CONST_ARRAY:
        return genComptimeArray(value, flags, expect_type);
    case CONST_ZERO_ARRAY:
        return genComptimeZeroArray(value, flags, expect_type);
    case CONST_STRING:
        return genComptimeString(value, flags);
    case CONST_STRUCT:
        return genComptimeStruct(value, flags, expect_type);
    case CONST_ENUM:
        return getPlatformIntConst(value->v_enum);
    default:
        Panic("unimplemented comptime value");
        break;
    }
}

static size_t const_id_counter = 0;

llvm::Constant* CodeGenerator::genComptimeArray(ConstValue* value, ComptimeGenFlags flags, Type* expect_type) {
    auto* ll_arr_data_type = llvm::ArrayType::get(
        genType(value->v_array.elem_type, true), 
        value->v_array.elems.size()
    );

    bool expect_array = expect_type->Inner()->kind == TYPE_ARRAY;
    bool expect_unwrapped_array = expect_array && (flags & CTG_UNWRAPPED);

    llvm::Constant* gv;
    if (value->v_array.alloc_loc == nullptr || expect_unwrapped_array) {
        std::vector<llvm::Constant*> ll_elems;
        for (auto* elem : value->v_array.elems) {
            ll_elems.push_back(genComptime(elem, flags | CTG_UNWRAPPED, value->v_array.elem_type));
        }

        auto* ll_array = llvm::ConstantArray::get(ll_arr_data_type, ll_elems);
        if (expect_unwrapped_array) {
            return ll_array;
        }

        gv = new llvm::GlobalVariable(
            mod, 
            ll_arr_data_type, 
            (bool)(flags & CTG_CONST), 
            (flags & CTG_EXPORTED) ? llvm::GlobalValue::ExternalLinkage : llvm::GlobalValue::PrivateLinkage, 
            ll_array, 
            std::format("__$const{}", const_id_counter++)
        );
    } else if (value->v_array.mod_id == src_mod.id) {
        gv = value->v_array.alloc_loc;
    } else {
        gv = new llvm::GlobalVariable(
            mod, 
            ll_arr_data_type, 
            llvm::dyn_cast<llvm::GlobalVariable>(value->v_array.alloc_loc)->isConstant(), 
            llvm::GlobalValue::ExternalLinkage, 
            nullptr, 
            value->v_array.alloc_loc->getName()
        );
    }

    value->v_array.alloc_loc = gv;
    value->v_array.mod_id = src_mod.id;

    if (expect_array) {
        return gv;
    }

    return llvm::ConstantStruct::get(ll_slice_type, { gv, getPlatformIntConst(value->v_array.elems.size()) });
}

llvm::Constant* CodeGenerator::genComptimeZeroArray(ConstValue* value, ComptimeGenFlags flags, Type* expect_type) {
    auto* ll_arr_data_type = llvm::ArrayType::get(
        genType(value->v_zarr.elem_type, true), 
        value->v_zarr.num_elems
    );

    bool expect_array = expect_type->Inner()->kind == TYPE_ARRAY;

    llvm::Constant* gv;
    if (expect_array && (flags & CTG_UNWRAPPED)) {
        return getNullValue(ll_arr_data_type);
    } else if (value->v_zarr.alloc_loc == nullptr) {
        gv = new llvm::GlobalVariable(
            mod, 
            ll_arr_data_type, 
            (bool)(flags & CTG_CONST), 
            (flags & CTG_EXPORTED) ? llvm::GlobalValue::ExternalLinkage : llvm::GlobalValue::PrivateLinkage,  
            getNullValue(ll_arr_data_type), 
            std::format("__$const{}", const_id_counter++)
        );
    } else if (value->v_zarr.mod_id == src_mod.id) {
        gv = value->v_zarr.alloc_loc;
    } else {
        gv = new llvm::GlobalVariable(
            mod, 
            ll_arr_data_type, 
            llvm::dyn_cast<llvm::GlobalVariable>(value->v_zarr.alloc_loc)->isConstant(), 
            llvm::GlobalValue::ExternalLinkage, 
            nullptr, 
            value->v_zarr.alloc_loc->getName()
        );
    }

    value->v_zarr.alloc_loc = gv;
    value->v_zarr.mod_id = src_mod.id;

    if (expect_array) {
        return gv;
    }

    return llvm::ConstantStruct::get(ll_slice_type, { gv, getPlatformIntConst(value->v_zarr.num_elems) });
}

llvm::Constant* CodeGenerator::genComptimeString(ConstValue* value, ComptimeGenFlags flags) {
    auto* ll_string_type = llvm::ArrayType::get(
        llvm::Type::getInt8Ty(ctx),
        value->v_str.value.size()
    );

    llvm::Constant* gv;
    
    if (value->v_str.alloc_loc == nullptr) {
        auto decoded = decodeStrLit(value->v_str.value);
        auto str_const = llvm::ConstantDataArray::getString(ctx, decoded, false);
        gv = new llvm::GlobalVariable(
            mod,
            str_const->getType(),
            (bool)(flags & CTG_CONST), 
            (flags & CTG_EXPORTED) ? llvm::GlobalValue::ExternalLinkage : llvm::GlobalValue::PrivateLinkage,
            str_const,
            std::format("__$const{}", const_id_counter++)
        );
    } else if (value->v_str.mod_id == src_mod.id) {
        gv = value->v_str.alloc_loc;
    } else {
        gv = new llvm::GlobalVariable(
            mod,
            value->v_str.alloc_loc->getType(),
            llvm::dyn_cast<llvm::GlobalVariable>(value->v_str.alloc_loc)->isConstant(), 
            llvm::GlobalValue::ExternalLinkage, 
            nullptr, 
            value->v_str.alloc_loc->getName()
        );
    }

    value->v_str.alloc_loc = gv;
    value->v_str.mod_id = src_mod.id;
    return llvm::ConstantStruct::get(ll_slice_type, { gv, getPlatformIntConst(value->v_str.value.size()) });
}

llvm::Constant* CodeGenerator::genComptimeStruct(ConstValue* value, ComptimeGenFlags flags, Type* expect_type) {
    auto* struct_type = expect_type->FullUnwrap();
    Assert(struct_type->kind == TYPE_STRUCT, "non-struct type struct constant in codegen");

    if ((flags & CTG_UNWRAPPED) || !shouldPtrWrap(struct_type)) {
        return genComptimeInnerStruct(value, flags, struct_type);
    }

    llvm::Constant* gv;
    if (value->v_struct.alloc_loc == nullptr) {
        auto* struct_const = genComptimeInnerStruct(value, flags, struct_type);
        gv = new llvm::GlobalVariable(
            mod,
            struct_const->getType(),
            (bool)(flags & CTG_CONST), 
            (flags & CTG_EXPORTED) ? llvm::GlobalValue::ExternalLinkage : llvm::GlobalValue::PrivateLinkage,
            struct_const,
            std::format("__$const{}", const_id_counter++)
        );
    } else if (value->v_struct.mod_id == src_mod.id) {
        gv = value->v_struct.alloc_loc;
    } else {
        gv = new llvm::GlobalVariable(
            mod,
            genType(struct_type, true),
            llvm::dyn_cast<llvm::GlobalVariable>(value->v_struct.alloc_loc)->isConstant(),
            llvm::GlobalValue::ExternalLinkage,
            nullptr,
            value->v_struct.alloc_loc->getName()
        );
    }

    value->v_struct.alloc_loc = gv;
    value->v_struct.mod_id = src_mod.id;
    return gv;
}

llvm::Constant* CodeGenerator::genComptimeInnerStruct(ConstValue* value, ComptimeGenFlags flags, Type* expect_type) {
    auto* struct_type = expect_type->FullUnwrap();
    Assert(struct_type->kind == TYPE_STRUCT, "non-struct type struct constant in codegen");

    std::vector<llvm::Constant*> field_values;  
    for (auto* field : value->v_struct.fields) {
        auto* field_type = struct_type->ty_Struct.fields[field_values.size()].type;
        field_values.push_back(genComptime(field, flags | CTG_UNWRAPPED, field_type));
    }

    return llvm::ConstantStruct::get(
        dyn_cast<llvm::StructType>(genType(struct_type, true)), 
        field_values
    );
}

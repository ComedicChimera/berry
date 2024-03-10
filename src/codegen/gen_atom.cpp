#include "codegen.hpp"

llvm::Value* CodeGenerator::genCall(AstExpr* node, llvm::Value* alloc_loc) {
    auto* func_ptr = genExpr(node->an_Call.func);

    // Handle global values.
    llvm::FunctionType* ll_func_type;
    if (func_ptr->getType()->isPointerTy()) {
        Assert(llvm::Function::classof(func_ptr), "call to a non-function type in codegen");

        auto* ll_func = llvm::dyn_cast<llvm::Function>(func_ptr);
        ll_func_type = ll_func->getFunctionType();
    } else {
        Assert(func_ptr->getType()->isFunctionTy(), "call to a non function type in codegen");
        ll_func_type = llvm::dyn_cast<llvm::FunctionType>(func_ptr->getType());
    }

    std::vector<llvm::Value*> args;
    for (auto& arg : node->an_Call.args) {
        args.push_back(genExpr(arg));
    }

    if (ll_func_type->getNumParams() > args.size()) {
        if (alloc_loc) {
            args.insert(args.begin(), alloc_loc);
            irb.CreateCall(ll_func_type, func_ptr, args);
            return nullptr;
        } else {
            auto* ret_val = genAlloc(node->type, node->an_Call.alloc_mode);
            args.insert(args.begin(), ret_val);
            irb.CreateCall(ll_func_type, func_ptr, args);
            return ret_val;
        }
    }

    return irb.CreateCall(ll_func_type, func_ptr, args);
}

llvm::Value* CodeGenerator::genIndexExpr(AstExpr* node, bool expect_addr) {
    auto& aindex = node->an_Index;

    Assert(aindex.array->type->Inner()->kind == TYPE_ARRAY, "index on non-array type in codegen");

    auto* array_val = genExpr(aindex.array);
    auto* index_val = genExpr(aindex.index);

    genBoundsCheck(index_val, getArrayLen(array_val));

    auto* ll_elem_type = genType(node->type->Inner(), true);
    auto* elem_ptr = irb.CreateGEP(llvm::ArrayType::get(ll_elem_type, 0), getArrayData(array_val), { getInt32Const(0), index_val });

    if (expect_addr || shouldPtrWrap(ll_elem_type)) {
        return elem_ptr;
    } else {
        return irb.CreateLoad(ll_elem_type, elem_ptr);
    }
}

llvm::Value* CodeGenerator::genSliceExpr(AstExpr* node, llvm::Value* alloc_loc) {
    auto& aslice = node->an_Slice;

    Assert(aslice.array->type->Inner()->kind == TYPE_ARRAY, "slice on non-array type in codegen");

    auto* array_val = genExpr(aslice.array);
    auto* len_val = getArrayLen(array_val);

    bool needs_bad_slice_check = aslice.start_index && aslice.end_index;

    llvm::Value* start_ndx_val;
    if (aslice.start_index) {
        start_ndx_val = genExpr(aslice.start_index);
        genBoundsCheck(start_ndx_val, len_val);
    } else {
        start_ndx_val = getPlatformIntConst(0);
    }

    llvm::Value* end_ndx_val;
    if (aslice.end_index) {
        end_ndx_val = genExpr(aslice.end_index);
        genBoundsCheck(end_ndx_val, len_val, true);
    } else {
        end_ndx_val = len_val;
    }

    if (needs_bad_slice_check) {
        auto* is_good_slice = irb.CreateICmpSLE(start_ndx_val, end_ndx_val);

        auto* bb_is_bad_slice = appendBlock();
        auto* bb_is_good_slice = appendBlock();

        irb.CreateCondBr(is_good_slice, bb_is_good_slice, bb_is_bad_slice);

        setCurrentBlock(bb_is_bad_slice);
        irb.CreateCall(ll_panic_badslice_func);
        irb.CreateUnreachable();

        setCurrentBlock(bb_is_good_slice);
    }

    auto* ll_elem_type = genType(node->type->ty_Array.elem_type, true);

    auto* new_arr_data = irb.CreateGEP(llvm::ArrayType::get(ll_elem_type, 0), getArrayData(array_val), { getInt32Const(0), start_ndx_val });
    auto* new_arr_len = irb.CreateSub(end_ndx_val, start_ndx_val);

    if (alloc_loc) {
        auto* data_ptr = getArrayDataPtr(alloc_loc);
        irb.CreateStore(new_arr_data, data_ptr);

        auto* len_ptr = getArrayLenPtr(alloc_loc);
        irb.CreateStore(new_arr_len, len_ptr);
        return nullptr;
    } else {
        auto* empty_array = llvm::Constant::getNullValue(ll_array_type);
        auto* new_array = irb.CreateInsertValue(empty_array, new_arr_data, 0);
        return irb.CreateInsertValue(new_array, new_arr_len, 1);
    }
}

llvm::Value* CodeGenerator::genFieldExpr(AstExpr* node, bool expect_addr) {
    auto& afield = node->an_Field;

    auto* root_inner_type = afield.root->type->Inner();

    if (expect_addr || shouldPtrWrap(node->type)) {
        llvm::Value* root_ptr;
        if (root_inner_type->kind == TYPE_PTR) {
            root_ptr = genExpr(afield.root);
            root_inner_type = root_inner_type->ty_Ptr.elem_type->Inner();
        } else {
            root_ptr = genExpr(afield.root, true);
        }

        if (root_inner_type->kind == TYPE_NAMED) {
            root_inner_type = root_inner_type->ty_Named.type;
        }

        switch (root_inner_type->kind) {
        case TYPE_STRUCT:
            return irb.CreateInBoundsGEP(
                genType(root_inner_type, true),
                root_ptr, 
                { getInt32Const(0), getInt32Const(afield.field_index)}
            );
        case TYPE_ARRAY:
            if (afield.field_name == "_ptr") {
                return getArrayDataPtr(root_ptr);
            } else if (afield.field_name == "_len") {
                return getArrayLenPtr(root_ptr);
            } else {
                Panic("bad array field {} in codegen", afield.field_name);
                return nullptr;
            }
            break;
        }
    } else {
        auto* root_val = genExpr(afield.root);

        if (root_inner_type->kind == TYPE_PTR) {
            root_inner_type = root_inner_type->ty_Ptr.elem_type->Inner();

            if (!shouldPtrWrap(root_inner_type)) {
                root_val = irb.CreateLoad(genType(root_inner_type), root_val);
            }
        }

        if (root_inner_type->kind == TYPE_NAMED) {
            root_inner_type = root_inner_type->ty_Named.type;
        }

        switch (root_inner_type->kind) {
        case TYPE_STRUCT:
            if (shouldPtrWrap(root_inner_type)) {
                auto* field_ptr = irb.CreateInBoundsGEP(
                    genType(root_inner_type, true),
                    root_val, 
                    { getInt32Const(0), getInt32Const(afield.field_index)}
                );

                return irb.CreateLoad(genType(root_inner_type->ty_Struct.fields[afield.field_index].type), field_ptr);
            } else {
                return irb.CreateExtractValue(root_val, afield.field_index);
            }
        case TYPE_ARRAY:
            if (afield.field_name == "_ptr") {
                return getArrayData(root_val);
            } else if (afield.field_name == "_len") {
                return getArrayLen(root_val);
            } else {
                Panic("bad array field {} in codegen", afield.field_name);
                return nullptr;
            }
            break;
        }
    }
    
    Panic("bad get field type in codegen");
    return nullptr;
}
/* -------------------------------------------------------------------------- */

llvm::Value* CodeGenerator::genArrayLit(AstExpr* node, llvm::Value* alloc_loc) {
    auto& array = node->an_Array;

    auto* ll_elem_type = genType(array.elems[0]->type, true);
    auto* len_val = getPlatformIntConst(array.elems.size());

    llvm::Value* data_val;
    auto* ll_const_array_type = llvm::ArrayType::get(ll_elem_type, array.elems.size());
    if (array.alloc_mode == A_ALLOC_HEAP) {
        Panic("heap allocation not implemented in codegen");
    } else if (array.alloc_mode == A_ALLOC_GLOBAL) {
        data_val = new llvm::GlobalVariable(
            mod,
            ll_const_array_type,
            false,
            llvm::GlobalValue::PrivateLinkage,
            getNullValue(ll_const_array_type)
        );
    } else {
        auto* curr_block = getCurrentBlock();
        setCurrentBlock(var_block);

        data_val = irb.CreateAlloca(ll_elem_type, len_val);

        setCurrentBlock(curr_block);
    } 

    for (int32_t i = 0; i < array.elems.size(); i++) {
        auto* elem_ptr = irb.CreateInBoundsGEP(ll_const_array_type, data_val, { getInt32Const(0), getPlatformIntConst(i) });
        
        genStoreExpr(array.elems[i], elem_ptr);
    }

    if (alloc_loc) {
        auto* data_ptr = getArrayDataPtr(alloc_loc);
        irb.CreateStore(data_val, data_ptr);

        auto* len_ptr = getArrayLenPtr(alloc_loc);
        irb.CreateStore(len_val, len_ptr);
        return nullptr;
    } else {
        llvm::Value* array = getNullValue(ll_array_type);
        array = irb.CreateInsertValue(array, data_val, 0);
        array = irb.CreateInsertValue(array, len_val, 1);
        return array;
    }
}

llvm::Value* CodeGenerator::genNewExpr(AstExpr* node, llvm::Value* alloc_loc) {
    auto& anew = node->an_New;
    
    auto* ll_elem_type = genType(anew.elem_type, true);
    llvm::Value* addr_val { nullptr };
    if (anew.size_expr) {
        // TODO: variable sizes/size checking
        Assert(node->an_New.size_expr->kind == AST_INT, "variable length arrays are not implemented in codegen");
        size_t array_len_val = (size_t)node->an_New.size_expr->an_Int.value;
        auto array_len = getPlatformIntConst(array_len_val);
        
        if (anew.alloc_mode == A_ALLOC_HEAP) {
            Panic("heap allocation not implemented in codegen");
        } else if (anew.alloc_mode == A_ALLOC_GLOBAL) {
            auto* ll_const_array_type = llvm::ArrayType::get(ll_elem_type, array_len_val);

            addr_val = new llvm::GlobalVariable(
                mod,
                ll_const_array_type,
                false,
                llvm::GlobalValue::PrivateLinkage,
                getNullValue(ll_const_array_type)
            );
        } else {
            auto* curr_block = getCurrentBlock();
            setCurrentBlock(var_block);

            addr_val = irb.CreateAlloca(ll_elem_type, array_len);
            irb.CreateMemSet(
                addr_val,
                getInt8Const(0),
                array_len_val * layout.getTypeAllocSize(ll_elem_type),
                layout.getPrefTypeAlign(ll_elem_type)
            );

            setCurrentBlock(curr_block);
        } 

        if (alloc_loc) {
            auto* data_ptr = getArrayDataPtr(alloc_loc);
            irb.CreateStore(addr_val, data_ptr);

            auto* len_ptr = getArrayLenPtr(alloc_loc);
            irb.CreateStore(array_len, len_ptr);
            return nullptr;
        } else {
            llvm::Value* array = getNullValue(ll_array_type);
            array = irb.CreateInsertValue(array, addr_val, 0);
            array = irb.CreateInsertValue(array, array_len, 1);
            return array;
        }
    } else {
        addr_val = genAlloc(ll_elem_type, anew.alloc_mode);

        irb.CreateMemSet(
            addr_val,
            getInt8Const(0),
            layout.getTypeAllocSize(ll_elem_type),
            layout.getPrefTypeAlign(ll_elem_type)
        );

        return addr_val;
    }
}

llvm::Value* CodeGenerator::genStructLit(AstExpr* node, llvm::Value* alloc_loc) {
    auto* struct_type = node->an_StructLitPos.root->type;
    auto* ll_struct_type = genType(struct_type, true);

    bool needs_alloc = alloc_loc == nullptr || node->kind > AST_STRUCT_LIT_NAMED;
    if (node->kind < AST_STRUCT_PTR_LIT_POS && needs_alloc && !shouldPtrWrap(ll_struct_type) ) {
        llvm::Value* lit_value = getNullValue(ll_struct_type);

        if (node->kind == AST_STRUCT_LIT_POS) {
            for (size_t i = 0; i < node->an_StructLitPos.field_inits.size(); i++) {
                lit_value = irb.CreateInsertValue(lit_value, genExpr(node->an_StructLitPos.field_inits[i]), i);
            }
        } else {
            for (auto& field_init : node->an_StructLitNamed.field_inits) {
                lit_value = irb.CreateInsertValue(lit_value, genExpr(field_init.expr), field_init.field_index);
            }
        }

        return lit_value;
    }

    if (needs_alloc) {
        alloc_loc = genAlloc(ll_struct_type, node->an_StructLitPos.alloc_mode);
    }

    if (node->kind == AST_STRUCT_LIT_POS || node->kind == AST_STRUCT_PTR_LIT_POS) {
        for (size_t i = 0; i < node->an_StructLitPos.field_inits.size(); i++) {
            auto* field_ptr = irb.CreateInBoundsGEP(
                ll_struct_type,
                alloc_loc,
                { getInt32Const(0), getInt32Const(i) }
            );

            genStoreExpr(node->an_StructLitPos.field_inits[i], field_ptr);
        }
    } else {
        for (auto& field_init : node->an_StructLitNamed.field_inits) {
            auto* field_ptr = irb.CreateInBoundsGEP(
                ll_struct_type, 
                alloc_loc, 
                { getInt32Const(0), getInt32Const(field_init.field_index)}
            );

            genStoreExpr(field_init.expr, field_ptr);
        }
    }

    return needs_alloc ? alloc_loc : nullptr;
}

/* -------------------------------------------------------------------------- */

static std::string decodeStrLit(std::string_view lit_val) {
    std::string decoded;

    for (int i = 0; i < lit_val.size(); i++) {
        auto c = lit_val[i];

        if (c == '\\') {
            c = lit_val[++i];
            switch (c) {
            case 'a':
                c = '\a';
                break;
            case 'b':
                c = '\b';
                break;
            case 'f':
                c = '\f';
                break;
            case 'n':
                c = '\n';
                break;
            case 'r':
                c = '\r';
                break;
            case 't':
                c = '\t';
                break;
            case 'v':
                c = '\v';
                break;
            case '0':
                c = '\0';
                break;
            }
        }

        decoded.push_back(c);
    }

    return decoded;
}

llvm::Value* CodeGenerator::genStrLit(AstExpr* node, llvm::Value* alloc_loc) {
    auto decoded = decodeStrLit(node->an_String.value);
    auto* str_constant = llvm::ConstantDataArray::getString(ctx, decoded, false);

    auto* gv_str_data = new llvm::GlobalVariable(
        mod,
        str_constant->getType(),
        true,
        llvm::GlobalValue::PrivateLinkage,
        str_constant
    );
    gv_str_data->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

    auto* len_const = getPlatformIntConst(decoded.size());

    if (alloc_loc) {
        auto data_ptr = getArrayDataPtr(alloc_loc);
        irb.CreateStore(gv_str_data, data_ptr);

        auto len_ptr = getArrayLenPtr(alloc_loc);
        irb.CreateStore(len_const, len_ptr);

        return nullptr;
    } else {
        auto* str_array_constant = llvm::ConstantStruct::get(ll_array_type, { gv_str_data, len_const });
        auto* gv_str = new llvm::GlobalVariable(
            mod,
            ll_array_type,
            true,
            llvm::GlobalValue::PrivateLinkage,
            str_array_constant
        );
        gv_str->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

        return irb.CreateLoad(ll_array_type, gv_str);
    }
}

llvm::Value* CodeGenerator::genIdent(AstExpr* node, bool expect_addr) {
    auto* symbol = node->an_Ident.symbol;
    Assert(symbol != nullptr, "unresolved symbol in codegen");

    llvm::Value* ll_value;
    if (symbol->parent_id != src_mod.id) {
        Assert((symbol->flags & SYM_EXPORTED) == 0, "unexported core symbol used in codegen");
        
        ll_value = loaded_imports.back()[symbol->def_number];
    } else {
        ll_value = symbol->llvm_value;
    }
    
    if ((symbol->flags & SYM_VAR) && !expect_addr && !shouldPtrWrap(node->type)) {
        return irb.CreateLoad(genType(node->type), ll_value);
    } else {
        return ll_value;
    }  
}

/* -------------------------------------------------------------------------- */

void CodeGenerator::genStoreExpr(AstExpr* node, llvm::Value* dest) {
    auto src = genExpr(node, false, dest);
    if (src != nullptr) {
        auto* ll_type = genType(node->type, true);
        if (node->IsLValue() && shouldPtrWrap(ll_type)) {
            genStructCopy(ll_type, src, dest);
        } else {
            irb.CreateStore(src, dest);
        }
    }
}

void CodeGenerator::genStructCopy(llvm::Type* llvm_struct_type, llvm::Value* src, llvm::Value* dest) {
    auto pref_align = layout.getPrefTypeAlign(llvm_struct_type);

    irb.CreateMemCpy(
        dest,
        pref_align,
        src,
        pref_align,
        getPlatformIntConst(getLLVMTypeByteSize(llvm_struct_type))
    );
}

llvm::Value* CodeGenerator::genAlloc(llvm::Type* llvm_type, AstAllocMode mode) {
    if (mode == A_ALLOC_STACK) {
        auto* curr_block = getCurrentBlock();
        setCurrentBlock(var_block);

        auto* alloc_value = irb.CreateAlloca(llvm_type);

        setCurrentBlock(curr_block);

        return alloc_value;
    } else if (mode == A_ALLOC_GLOBAL) {
        new llvm::GlobalVariable(
            mod,
            llvm_type,
            false,
            llvm::GlobalValue::PrivateLinkage,
            getNullValue(llvm_type)
        );
    } else {
        Panic("heap allocation is not yet implemented");
    }
}

/* -------------------------------------------------------------------------- */

void CodeGenerator::genBoundsCheck(llvm::Value* ndx, llvm::Value* arr_len, bool can_equal_len) {
    auto* is_ge_zero = irb.CreateICmpSGE(ndx, llvm::Constant::getNullValue(ndx->getType()));
    llvm::Value* is_lt_len;
    if (can_equal_len) {
        is_lt_len = irb.CreateICmpSLE(ndx, arr_len);
    } else {
        is_lt_len = irb.CreateICmpSLT(ndx, arr_len);
    }
    auto* is_in_bounds = irb.CreateAnd(is_ge_zero, is_lt_len);

    auto* bb_oob = appendBlock();
    auto* bb_in_bounds = appendBlock();

    irb.CreateCondBr(is_in_bounds, bb_in_bounds, bb_oob);
    
    setCurrentBlock(bb_oob);
    irb.CreateCall(ll_panic_oob_func);
    irb.CreateUnreachable();

    setCurrentBlock(bb_in_bounds);
}

llvm::Value* CodeGenerator::getArrayData(llvm::Value* array) {
    return irb.CreateExtractValue(array, 0);
}

llvm::Value* CodeGenerator::getArrayLen(llvm::Value* array) {
    return irb.CreateExtractValue(array, 1);
}

llvm::Value* CodeGenerator::getArrayDataPtr(llvm::Value* array_ptr) {
    return irb.CreateInBoundsGEP(ll_array_type, array_ptr, { getInt32Const(0), getInt32Const(0) });
}

llvm::Value* CodeGenerator::getArrayLenPtr(llvm::Value* array_ptr) {
    return irb.CreateInBoundsGEP(ll_array_type, array_ptr, { getInt32Const(0), getInt32Const(1) });
}

/* -------------------------------------------------------------------------- */

llvm::Constant* CodeGenerator::getNullValue(Type* type) {
    return llvm::Constant::getNullValue(genType(type));
}

llvm::Constant* CodeGenerator::getNullValue(llvm::Type* ll_type) {
    return llvm::Constant::getNullValue(ll_type);
}

llvm::Constant* CodeGenerator::getInt32Const(uint32_t value) {
    llvm::APInt ap_int(32, (uint64_t)value, false);
    return llvm::Constant::getIntegerValue(llvm::Type::getInt32Ty(ctx), ap_int);
}

llvm::Constant* CodeGenerator::getInt8Const(uint8_t value) {
    llvm::APInt ap_int(8, (uint64_t)value, false);
    return llvm::Constant::getIntegerValue(llvm::Type::getInt8Ty(ctx), ap_int);
}

llvm::Constant* CodeGenerator::getPlatformIntConst(uint64_t value) {
    // TODO: 32-bit systems...
    llvm::APInt ap_int(64, value, false);
    return llvm::Constant::getIntegerValue(llvm::Type::getInt64Ty(ctx), ap_int);
}

llvm::Constant* CodeGenerator::makeLLVMIntLit(Type* int_type, uint64_t value) {
    if (int_type->kind == TYPE_BOOL) {
        llvm::APInt ap_int(1, value, false);
        return llvm::Constant::getIntegerValue(llvm::Type::getInt1Ty(ctx), ap_int);
    } else if (int_type->kind == TYPE_INT) {
        llvm::APInt ap_int(int_type->ty_Int.bit_size, value, int_type->ty_Int.is_signed);
        return llvm::Constant::getIntegerValue(genType(int_type), ap_int);
    } else {
        Panic("invalid type {} to make an integer literal in codegen", int_type->ToString());
    }
}

llvm::Constant* CodeGenerator::makeLLVMFloatLit(Type* float_type, double value) {
    Assert(float_type->kind == TYPE_FLOAT, "invalid type {} to make a float literal in codegen", float_type->ToString());

    if (float_type->ty_Float.bit_size == 64) {
        llvm::APFloat ap_float(value);
        return llvm::ConstantFP::get(ctx, ap_float);
    } else {
        llvm::APFloat ap_float((float)value);
        return llvm::ConstantFP::get(ctx, ap_float);
    }
}
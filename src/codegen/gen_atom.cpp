#include "codegen.hpp"

llvm::Value* CodeGenerator::genCall(HirExpr* node, llvm::Value* alloc_loc) {
    auto* func_ptr = genExpr(node->ir_Call.func);

    // Handle global values.
    llvm::FunctionType* ll_func_type;
    if (func_ptr->getType()->isPointerTy() && llvm::Function::classof(func_ptr)) {
        auto* ll_func = llvm::dyn_cast<llvm::Function>(func_ptr);
        ll_func_type = ll_func->getFunctionType();
    } else {
        ll_func_type = genFuncType(node->ir_Call.func->type);
    }

    std::vector<llvm::Value*> args;
    for (auto& arg : node->ir_Call.args) {
        args.push_back(genExpr(arg));
    }

    if (ll_func_type->getNumParams() > args.size()) {
        if (alloc_loc) {
            args.insert(args.begin(), alloc_loc);
            irb.CreateCall(ll_func_type, func_ptr, args);
            return nullptr;
        } else {
            auto* ret_val = genAlloc(node->type, node->ir_Call.alloc_mode);
            args.insert(args.begin(), ret_val);
            irb.CreateCall(ll_func_type, func_ptr, args);
            return ret_val;
        }
    }

    return irb.CreateCall(ll_func_type, func_ptr, args);
}

llvm::Value* CodeGenerator::genIndexExpr(HirExpr* node, bool expect_addr) {
    auto& hindex = node->ir_Index;

    auto* x_type = hindex.expr->type->Inner();
    auto* x_val = genExpr(hindex.expr);

    auto* index_val = genExpr(hindex.index);
    auto* ll_elem_type = genType(node->type->Inner(), true);

    llvm::Value* elem_ptr;
    if (x_type->kind == TYPE_ARRAY) {
        genBoundsCheck(index_val, getPlatformIntConst(x_type->ty_Array.len));

        elem_ptr = irb.CreateGEP(ll_elem_type, x_val, { index_val });
    } else {
        genBoundsCheck(index_val, getSliceLen(x_val));

        elem_ptr = irb.CreateGEP(ll_elem_type, getSliceData(x_val), { index_val });
    }    

    if (expect_addr || shouldPtrWrap(ll_elem_type)) {
        return elem_ptr;
    } else {
        return irb.CreateLoad(ll_elem_type, elem_ptr);
    }
}

llvm::Value* CodeGenerator::genSliceExpr(HirExpr* node, llvm::Value* alloc_loc) {
    auto& hslice = node->ir_Slice;

    auto* x_type = hslice.expr->type->Inner();
    auto* x_val = genExpr(hslice.expr);

    llvm::Value* len_val;
    if (x_type->kind == TYPE_ARRAY) {
        len_val = getPlatformIntConst(x_type->ty_Array.len);
    } else {
        len_val = getSliceLen(x_val);
    }

    bool needs_bad_slice_check = hslice.start_index && hslice.end_index;

    llvm::Value* start_ndx_val;
    if (hslice.start_index) {
        start_ndx_val = genExpr(hslice.start_index);
        genBoundsCheck(start_ndx_val, len_val);
    } else {
        start_ndx_val = getPlatformIntConst(0);
    }

    llvm::Value* end_ndx_val;
    if (hslice.end_index) {
        end_ndx_val = genExpr(hslice.end_index);
        genBoundsCheck(end_ndx_val, len_val, true);
    } else {
        end_ndx_val = len_val;
    }

    if (needs_bad_slice_check) {
        auto* is_good_slice = irb.CreateICmpSLE(start_ndx_val, end_ndx_val);
        is_good_slice = genLLVMExpect(is_good_slice, makeLLVMIntLit(&prim_bool_type, 1));

        auto* bb_is_bad_slice = appendBlock();
        auto* bb_is_good_slice = appendBlock();

        irb.CreateCondBr(is_good_slice, bb_is_good_slice, bb_is_bad_slice);
        setCurrentBlock(bb_is_bad_slice);

        if (rtstub_panic_badslice)
            rtstub_panic_badslice = genPanicStub("__berry_panic_badslice");

        irb.CreateCall(rtstub_panic_badslice);
        irb.CreateUnreachable();

        setCurrentBlock(bb_is_good_slice);
    }

    auto* ll_elem_type = genType(node->type->ty_Slice.elem_type, true);
    llvm::Value* new_slice_data;
    if (x_type->kind == TYPE_ARRAY) {
        new_slice_data = irb.CreateGEP(ll_elem_type, x_val, { start_ndx_val });
    } else {
        new_slice_data = irb.CreateGEP(ll_elem_type, getSliceData(x_val), { start_ndx_val });
    }

    auto* new_slice_len = irb.CreateSub(end_ndx_val, start_ndx_val);
    if (alloc_loc) {
        auto* data_ptr = getSliceDataPtr(alloc_loc);
        irb.CreateStore(new_slice_data, data_ptr);

        auto* len_ptr = getSliceLenPtr(alloc_loc);
        irb.CreateStore(new_slice_len, len_ptr);
        return nullptr;
    } else {
        auto* empty_array = llvm::Constant::getNullValue(ll_slice_type);
        auto* new_array = irb.CreateInsertValue(empty_array, new_slice_data, 0);
        return irb.CreateInsertValue(new_array, new_slice_len, 1);
    }
}

llvm::Value* CodeGenerator::genFieldExpr(HirExpr* node, bool expect_addr) {
    auto& hfield = node->ir_Field;

    auto* x_type = hfield.expr->type->FullUnwrap();

    if (expect_addr || shouldPtrWrap(node->type)) {
        llvm::Value* x_ptr;
        if (node->kind == HIR_DEREF_FIELD) {
            x_ptr = genExpr(hfield.expr);
            x_type = x_type->ty_Ptr.elem_type->FullUnwrap();
        } else {
            x_ptr = genExpr(hfield.expr, true);
        }

        switch (x_type->kind) {
        case TYPE_STRUCT:
            return irb.CreateInBoundsGEP(
                genType(x_type, true),
                x_ptr, 
                { getInt32Const(0), getInt32Const(hfield.field_index)}
            );
        case TYPE_ARRAY:
            if (hfield.field_index == 0) {
                return x_ptr;
            } else if (hfield.field_index == 1) {
                Panic("expect_addr used for array._len");
            }
        case TYPE_SLICE:
        case TYPE_STRING:
            if (hfield.field_index == 0) {
                return getSliceDataPtr(x_ptr);
            } else if (hfield.field_index == 1) {
                return getSliceLenPtr(x_ptr);
            } 
            break;
        }
    } else {
        auto* x_val = genExpr(hfield.expr);

        if (node->kind == HIR_DEREF_FIELD) {
            x_type = x_type->ty_Ptr.elem_type->FullUnwrap();

            if (!shouldPtrWrap(x_type)) {
                x_val = irb.CreateLoad(genType(x_type), x_val);
            }
        }

        switch (x_type->kind) {
        case TYPE_STRUCT:
            if (shouldPtrWrap(x_type)) {
                auto* field_ptr = irb.CreateInBoundsGEP(
                    genType(x_type, true),
                    x_val, 
                    { getInt32Const(0), getInt32Const(hfield.field_index)}
                );

                return irb.CreateLoad(genType(x_type->ty_Struct.fields[hfield.field_index].type), field_ptr);
            } else {
                return irb.CreateExtractValue(x_val, hfield.field_index);
            }
        case TYPE_ARRAY:
            if (hfield.field_index == 0) {
                return x_val;
            } else if (hfield.field_index == 1) {
                return getPlatformIntConst(x_type->ty_Array.len);
            } 
        case TYPE_SLICE:
        case TYPE_STRING:
            if (hfield.field_index == 0) {
                return getSliceData(x_val);
            } else if (hfield.field_index == 1) {
                return getSliceLen(x_val);
            }
            break;
        case TYPE_FUNC:
            // TODO: handle function unboxing as necessary
            return x_val;
        }
    }
    
    Panic("bad get field in codegen");
    return nullptr;
}

/* -------------------------------------------------------------------------- */

llvm::Value* CodeGenerator::genNewExpr(HirExpr* node) {
    auto& hnew = node->ir_New;
    
    auto* ll_elem_type = genType(hnew.elem_type, true);
    auto* data_ptr = genAlloc(ll_elem_type, hnew.alloc_mode);

    irb.CreateMemSet(
        data_ptr,
        getInt8Const(0),
        getLLVMByteSize(ll_elem_type),
        layout.getPrefTypeAlign(ll_elem_type)
    );

    return data_ptr;
}

llvm::Value* CodeGenerator::genNewArray(HirExpr* node, llvm::Value* alloc_loc) {
    auto& hnew = node->ir_NewArray;

    auto* ll_elem_type = genType(node->type->ty_Slice.elem_type, true);

    llvm::Value* data_ptr;
    llvm::Value* len_val;
    if (hnew.const_len) {
        len_val = getPlatformIntConst(hnew.const_len);
        auto* ll_array_type = llvm::ArrayType::get(ll_elem_type, hnew.const_len);

        if (hnew.alloc_mode == HIRMEM_HEAP) {
            Panic("heap allocation not implemented in codegen");
        } else if (hnew.alloc_mode == HIRMEM_GLOBAL) {

            data_ptr = new llvm::GlobalVariable(
                mod,
                ll_array_type,
                false,
                llvm::GlobalValue::PrivateLinkage,
                getNullValue(ll_array_type)
            );
        } else {
            auto* curr_block = getCurrentBlock();
            setCurrentBlock(var_block);

            data_ptr = irb.CreateAlloca(ll_elem_type, len_val);
            irb.CreateMemSet(
                data_ptr,
                getInt8Const(0),
                getLLVMByteSize(ll_array_type),
                layout.getPrefTypeAlign(ll_elem_type)
            );

            setCurrentBlock(curr_block);
        } 
    } else {
        Panic("heap allocation ot implemented in codegen");
    }
    
    if (alloc_loc) {
        auto* slice_data_ptr = getSliceDataPtr(alloc_loc);
        irb.CreateStore(data_ptr, slice_data_ptr);

        auto* len_ptr = getSliceLenPtr(alloc_loc);
        irb.CreateStore(len_val, len_ptr);
        return nullptr;
    } else {
        llvm::Value* array = getNullValue(ll_slice_type);
        array = irb.CreateInsertValue(array, data_ptr, 0);
        array = irb.CreateInsertValue(array, len_val, 1);
        return array;
    }
}

llvm::Value* CodeGenerator::genNewStruct(HirExpr* node) {
    auto* struct_type = node->type->ty_Ptr.elem_type->FullUnwrap();
    auto* ll_struct_type = genType(struct_type, true);

    auto& hnew = node->ir_StructLit;
    auto* struct_ptr = genAlloc(ll_struct_type, hnew.alloc_mode);

    irb.CreateMemSet(
        struct_ptr,
        getInt8Const(0),
        getLLVMByteSize(ll_struct_type),
        layout.getPrefTypeAlign(ll_struct_type)
    );

    for (auto& hfield : hnew.field_inits) {
        auto* field_ptr = irb.CreateInBoundsGEP(ll_struct_type, struct_ptr, { getInt32Const(0), getInt32Const(hfield.field_index) });
        genStoreExpr(hfield.expr, field_ptr);
    }

    return struct_ptr;
}

llvm::Value* CodeGenerator::genArrayLit(HirExpr* node, llvm::Value* alloc_loc) {
    auto& array = node->ir_ArrayLit;

    auto* ll_elem_type = genType(array.items[0]->type, true);
    auto* ll_array_type = llvm::ArrayType::get(ll_elem_type, array.items.size());
    auto* len_val = getPlatformIntConst(array.items.size());

    llvm::Value* data_ptr;
    if (node->type->kind == TYPE_ARRAY && alloc_loc) {
        data_ptr = alloc_loc;
    } else {
        if (array.alloc_mode == HIRMEM_HEAP) {
            Panic("heap allocation not implemented in codegen");
        } else if (array.alloc_mode == HIRMEM_GLOBAL) {
            data_ptr = new llvm::GlobalVariable(
                mod,
                ll_array_type,
                false,
                llvm::GlobalValue::PrivateLinkage,
                getNullValue(ll_array_type)
            );
        } else {
            auto* curr_block = getCurrentBlock();
            setCurrentBlock(var_block);

            data_ptr = irb.CreateAlloca(ll_elem_type, len_val);

            setCurrentBlock(curr_block);
        } 
    }

    for (size_t i = 0; i < array.items.size(); i++) {
        auto* elem_ptr = irb.CreateInBoundsGEP(ll_array_type, data_ptr, { getInt32Const(0), getPlatformIntConst(i) });
        
        genStoreExpr(array.items[i], elem_ptr);
    }

    if (node->type->kind == TYPE_ARRAY) {
        if (alloc_loc) {
            return nullptr;
        } else {
            return data_ptr;
        }
    } else {
        if (alloc_loc) {
            auto* slice_data_ptr = getSliceDataPtr(alloc_loc);
            irb.CreateStore(data_ptr, slice_data_ptr);

            auto* slice_len_ptr = getSliceLenPtr(alloc_loc);
            irb.CreateStore(len_val, slice_len_ptr);
            return nullptr;
        } else {
            llvm::Value* array = getNullValue(ll_slice_type);
            array = irb.CreateInsertValue(array, data_ptr, 0);
            array = irb.CreateInsertValue(array, len_val, 1);
            return array;
        }
    }    
}

llvm::Value* CodeGenerator::genStructLit(HirExpr* node, llvm::Value* alloc_loc) {
    auto* struct_type = node->type->FullUnwrap();
    auto* ll_struct_type = genType(struct_type, true);

    auto& hstruct = node->ir_StructLit;
    if (shouldPtrWrap(ll_struct_type)) {
        llvm::Value* struct_ptr;
        if (alloc_loc) {
            struct_ptr = alloc_loc;
        } else {
            struct_ptr = genAlloc(ll_struct_type, node->ir_StructLit.alloc_mode);
        }

        irb.CreateMemSet(
            struct_ptr,
            getInt8Const(0),
            getLLVMByteSize(ll_struct_type),
            layout.getPrefTypeAlign(ll_struct_type)
        );

        for (auto& hfield : hstruct.field_inits) {
            auto* field_ptr = irb.CreateInBoundsGEP(ll_struct_type, struct_ptr, { getInt32Const(0), getInt32Const(hfield.field_index) });
            genStoreExpr(hfield.expr, field_ptr);
        }

        if (alloc_loc) {
            return nullptr;
        } else {
            return struct_ptr;
        }
    } else {
        llvm::Value* struct_value = getNullValue(ll_struct_type);
        for (auto& hfield : hstruct.field_inits) {
            struct_value = irb.CreateInsertValue(struct_value, genExpr(hfield.expr), hfield.field_index);
        }
        return struct_value;
    }
}

llvm::Value* CodeGenerator::genStringLit(HirExpr* node, llvm::Value* alloc_loc) {
    auto decoded = decodeStrLit(node->ir_String.value);
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
        auto data_ptr = getSliceDataPtr(alloc_loc);
        irb.CreateStore(gv_str_data, data_ptr);

        auto len_ptr = getSliceLenPtr(alloc_loc);
        irb.CreateStore(len_const, len_ptr);

        return nullptr;
    } else {
        auto* str_array_constant = llvm::ConstantStruct::get(ll_slice_type, { gv_str_data, len_const });
        auto* gv_str = new llvm::GlobalVariable(
            mod,
            ll_slice_type,
            true,
            llvm::GlobalValue::PrivateLinkage,
            str_array_constant
        );
        gv_str->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

        return irb.CreateLoad(ll_slice_type, gv_str);
    }
}

llvm::Value* CodeGenerator::genIdent(HirExpr* node, bool expect_addr) {
    auto* symbol = node->ir_Ident.symbol;
    Assert(symbol != nullptr, "unresolved symbol in codegen");

    llvm::Value* ll_value;
    if (symbol->parent_id != src_mod.id) {
        Assert((symbol->flags & SYM_EXPORTED) != 0, "unexported core symbol used in codegen");
        
        ll_value = loaded_imports.back()[symbol->decl_number];
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

void CodeGenerator::genStoreExpr(HirExpr* node, llvm::Value* dest) {
    auto src = genExpr(node, false, dest);
    if (src != nullptr) {
        auto* ll_type = genType(node->type, true);
        if (shouldPtrWrap(ll_type)) {
            genMemCopy(ll_type, src, dest);
        } else {
            irb.CreateStore(src, dest);
        }
    }
}

void CodeGenerator::genMemCopy(llvm::Type* ll_type, llvm::Value* src, llvm::Value* dest) {
    auto pref_align = layout.getPrefTypeAlign(ll_type);

    irb.CreateMemCpy(
        dest,
        pref_align,
        src,
        pref_align,
        getPlatformIntConst(getLLVMByteSize(ll_type))
    );
}

llvm::Value* CodeGenerator::genAlloc(llvm::Type* llvm_type, HirAllocMode mode) {
    if (mode == HIRMEM_STACK) {
        auto* curr_block = getCurrentBlock();
        setCurrentBlock(var_block);

        auto* alloc_value = irb.CreateAlloca(llvm_type);

        setCurrentBlock(curr_block);

        return alloc_value;
    } else if (mode == HIRMEM_GLOBAL) {
        return new llvm::GlobalVariable(
            mod,
            llvm_type,
            false,
            llvm::GlobalValue::PrivateLinkage,
            getNullValue(llvm_type)
        );
    } else {
        Panic("heap allocation is not yet implemented");
        return nullptr;
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
    is_in_bounds = genLLVMExpect(is_in_bounds, makeLLVMIntLit(&prim_bool_type, 1));

    auto* bb_oob = appendBlock();
    auto* bb_in_bounds = appendBlock();

    irb.CreateCondBr(is_in_bounds, bb_in_bounds, bb_oob);
    setCurrentBlock(bb_oob);

    if (rtstub_panic_oob == nullptr)
        rtstub_panic_oob = genPanicStub("__berry_panic_oob");

    irb.CreateCall(rtstub_panic_oob);
    irb.CreateUnreachable();

    setCurrentBlock(bb_in_bounds);
}

/* -------------------------------------------------------------------------- */

llvm::Value* CodeGenerator::getSliceData(llvm::Value* slice) {
    return irb.CreateExtractValue(slice, 0);
}

llvm::Value* CodeGenerator::getSliceLen(llvm::Value* slice) {
    return irb.CreateExtractValue(slice, 1);
}

llvm::Value* CodeGenerator::getSliceDataPtr(llvm::Value* slice_ptr) {
    return irb.CreateInBoundsGEP(ll_slice_type, slice_ptr, { getInt32Const(0), getInt32Const(0) });
}

llvm::Value* CodeGenerator::getSliceLenPtr(llvm::Value* slice_ptr) {
    return irb.CreateInBoundsGEP(ll_slice_type, slice_ptr, { getInt32Const(0), getInt32Const(1) });
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
    llvm::APInt ap_int(ll_platform_int_type->getBitWidth(), value, false);
    return llvm::Constant::getIntegerValue(ll_platform_int_type, ap_int);
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

std::string CodeGenerator::decodeStrLit(std::string_view lit_val) {
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
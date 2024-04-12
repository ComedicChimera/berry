#include "codegen.hpp"

llvm::Value* CodeGenerator::genCall(HirExpr* node, llvm::Value* alloc_loc) {
    auto* func_ptr = genExpr(node->ir_Call.func);

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
    auto& aindex = node->ir_Index;

    auto* x_type = aindex.expr->type->Inner();
    auto* x_val = genExpr(aindex.expr);

    auto* index_val = genExpr(aindex.index);
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
    auto& aslice = node->ir_Slice;

    auto* x_type = aslice.expr->type->Inner();
    auto* x_val = genExpr(aslice.expr);

    llvm::Value* len_val;
    if (x_type->kind == TYPE_ARRAY) {
        len_val = getPlatformIntConst(x_type->ty_Array.len);
    } else {
        len_val = getSliceLen(x_val);
    }

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
    auto& afield = node->ir_Field;

    auto* x_type = afield.expr->type->FullUnwrap();

    if (expect_addr || shouldPtrWrap(node->type)) {
        llvm::Value* x_ptr;
        if (node->kind == HIR_DEREF_FIELD) {
            x_ptr = genExpr(afield.expr);
            x_type = x_type->ty_Ptr.elem_type->FullUnwrap();
        } else {
            x_ptr = genExpr(afield.expr, true);
        }

        switch (x_type->kind) {
        case TYPE_STRUCT:
            return irb.CreateInBoundsGEP(
                genType(x_type, true),
                x_ptr, 
                { getInt32Const(0), getInt32Const(afield.field_index)}
            );
        case TYPE_ARRAY:
            if (afield.field_index == 0) {
                return x_ptr;
            } else if (afield.field_index == 1) {
                Panic("expect_addr used for array._len");
            }
        case TYPE_SLICE:
        case TYPE_STRING:
            if (afield.field_index == 0) {
                return getSliceDataPtr(x_ptr);
            } else if (afield.field_index == 1) {
                return getSliceLenPtr(x_ptr);
            } 
            break;
        }
    } else {
        auto* x_val = genExpr(afield.expr);

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
                    { getInt32Const(0), getInt32Const(afield.field_index)}
                );

                return irb.CreateLoad(genType(x_type->ty_Struct.fields[afield.field_index].type), field_ptr);
            } else {
                return irb.CreateExtractValue(x_val, afield.field_index);
            }
        case TYPE_ARRAY:
            if (afield.field_index == 0) {
                return x_val;
            } else if (afield.field_index == 1) {
                return getPlatformIntConst(x_type->kind == TYPE_ARRAY);
            } 
        case TYPE_SLICE:
        case TYPE_STRING:
            if (afield.field_index == 0) {
                return getSliceData(x_val);
            } else if (afield.field_index == 1) {
                return getSliceLen(x_val);
            }
            break;
        }
    }
    
    Panic("bad get field in codegen");
    return nullptr;
}

/* -------------------------------------------------------------------------- */

llvm::Value* CodeGenerator::genArrayLit(HirExpr* node, llvm::Value* alloc_loc) {
    auto& array = node->ir_Array;

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
    auto& anew = node->ir_New;
    
    auto* ll_elem_type = genType(anew.elem_type, true);
    llvm::Value* addr_val { nullptr };
    if (anew.size_expr) {
        // TODO: variable sizes/size checking
        Assert(node->ir_New.size_expr->kind == AST_INT, "variable length arrays are not implemented in codegen");
        size_t array_len_val = (size_t)node->ir_New.size_expr->ir_Int.value;
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
                array_len_val * getLLVMByteSize(ll_elem_type),
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
            getLLVMByteSize(ll_elem_type),
            layout.getPrefTypeAlign(ll_elem_type)
        );

        return addr_val;
    }
}

llvm::Value* CodeGenerator::genStructLit(AstExpr* node, llvm::Value* alloc_loc) {
    auto* struct_type = node->ir_StructLitPos.root->type;
    auto* ll_struct_type = genType(struct_type, true);

    bool needs_alloc = alloc_loc == nullptr || node->kind > AST_STRUCT_LIT_NAMED;
    if (node->kind < AST_STRUCT_PTR_LIT_POS && needs_alloc && !shouldPtrWrap(ll_struct_type) ) {
        llvm::Value* lit_value = getNullValue(ll_struct_type);

        if (node->kind == AST_STRUCT_LIT_POS) {
            for (size_t i = 0; i < node->ir_StructLitPos.field_inits.size(); i++) {
                lit_value = irb.CreateInsertValue(lit_value, genExpr(node->ir_StructLitPos.field_inits[i]), i);
            }
        } else {
            for (auto& field_init : node->ir_StructLitNamed.field_inits) {
                lit_value = irb.CreateInsertValue(lit_value, genExpr(field_init.expr), field_init.field_index);
            }
        }

        return lit_value;
    }

    if (needs_alloc) {
        alloc_loc = genAlloc(ll_struct_type, node->ir_StructLitPos.alloc_mode);
    }

    if (node->kind == AST_STRUCT_LIT_POS || node->kind == AST_STRUCT_PTR_LIT_POS) {
        for (size_t i = 0; i < node->ir_StructLitPos.field_inits.size(); i++) {
            auto* field_ptr = irb.CreateInBoundsGEP(
                ll_struct_type,
                alloc_loc,
                { getInt32Const(0), getInt32Const(i) }
            );

            genStoreExpr(node->ir_StructLitPos.field_inits[i], field_ptr);
        }
    } else {
        for (auto& field_init : node->ir_StructLitNamed.field_inits) {
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

llvm::Value* CodeGenerator::genStrLit(AstExpr* node, llvm::Value* alloc_loc) {
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
    auto* symbol = node->ir_Ident.symbol;
    Assert(symbol != nullptr, "unresolved symbol in codegen");

    llvm::Value* ll_value;
    if (symbol->parent_id != src_mod.id) {
        Assert((symbol->flags & SYM_EXPORTED) != 0, "unexported core symbol used in codegen");
        
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
        if (shouldPtrWrap(ll_type)) {
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
        getPlatformIntConst(getLLVMByteSize(llvm_struct_type))
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

    auto* bb_oob = appendBlock();
    auto* bb_in_bounds = appendBlock();

    irb.CreateCondBr(is_in_bounds, bb_in_bounds, bb_oob);
    
    setCurrentBlock(bb_oob);
    irb.CreateCall(rtstub_panic_oob);
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
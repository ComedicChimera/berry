#include "codegen.hpp"

llvm::Value* CodeGenerator::genExpr(AstExpr* node, bool expect_addr, llvm::Value* alloc_loc) {
    debug.SetDebugLocation(node->span);

    switch (node->kind) {
    case AST_CAST:
        return genCast(node);
    case AST_BINOP:
        return genBinop(node);
    case AST_UNOP:
        return genUnop(node);
    case AST_ADDR:
        Assert(node->an_Addr.elem->IsLValue(), "tried to take address of r-value in codegen");
        return genExpr(node->an_Addr.elem, true);
    case AST_DEREF: {
        auto ptr_val = genExpr(node->an_Deref.ptr);
    
        if (expect_addr) {
            return ptr_val;
        } else {
            return irb.CreateLoad(genType(node->type), ptr_val);
        }
    } break;
    case AST_CALL:
        return genCall(node, alloc_loc);
    case AST_INDEX:
        return genIndexExpr(node, expect_addr);
    case AST_SLICE:
        return genSliceExpr(node, alloc_loc);
    case AST_FIELD:
        return genFieldExpr(node, expect_addr);
    case AST_ARRAY:
        return genArrayLit(node, alloc_loc);
    case AST_NEW:
        return genNewExpr(node, alloc_loc);
    case AST_IDENT: {
        auto* symbol = node->an_Ident.symbol;
        Assert(symbol != nullptr, "unresolved symbol in codegen");

        if (symbol->kind == SYM_VARIABLE && !expect_addr) {
            return irb.CreateLoad(genType(symbol->type), symbol->llvm_value);
        }

        return symbol->llvm_value;
    } break;
    case AST_INT: {
        // NOTE: It is possible for an int literal to have a float type if a
        // number literal implicitly takes on a floating point value.
        auto inner_type = node->type->Inner();
        switch (inner_type->kind) {
        case TYPE_INT: 
            return makeLLVMIntLit(inner_type, node->an_Int.value);
        case TYPE_FLOAT: 
            return makeLLVMFloatLit(inner_type, (double)node->an_Int.value);
        default:
            Panic("non-numeric type integer literal in codegen");
        }    
    } break;
    case AST_FLOAT: {
        return makeLLVMFloatLit(node->type->Inner(), node->an_Float.value);
    } break;
    case AST_BOOL:
        return makeLLVMIntLit(node->type, (uint64_t)node->an_Bool.value);
    case AST_NULL:
        return getNullValue(genType(node->type));
    case AST_STR:
        return genStrLit(node, alloc_loc);
    default:
        Panic("expr codegen not implemented for {}", (int)node->kind);
        break;
    }

    return nullptr;
}

/* -------------------------------------------------------------------------- */

llvm::Value* CodeGenerator::genCast(AstExpr* node) {
    auto& acast = node->an_Cast;

    auto* src_val = genExpr(acast.src);
    if (tctx.Equal(acast.src->type, node->type)) {
        return src_val;
    }

    auto* src_type = acast.src->type->Inner();
    auto* dest_type = node->type->Inner();

    auto src_kind = src_type->kind;
    auto dest_kind = dest_type->kind;

    auto* ll_dtype = genType(dest_type);
    switch (dest_kind) {
    case TYPE_INT: {
        if (src_kind == TYPE_INT) {
            return irb.CreateIntCast(src_val, ll_dtype, src_type->ty_Int.is_signed);
        } else if (src_kind == TYPE_FLOAT) {
            if (dest_type->ty_Int.is_signed) {
                return irb.CreateFPToSI(src_val, ll_dtype);
            } else {
                return irb.CreateFPToUI(src_val, ll_dtype);
            }
        } else if (src_kind == TYPE_BOOL) {
            return irb.CreateZExt(src_val, ll_dtype);
        } else if (src_kind == TYPE_PTR) {
            return irb.CreatePtrToInt(src_val, ll_dtype);
        }
    } break; 
    case TYPE_FLOAT:
        if (src_kind == TYPE_INT) {
            if (src_type->ty_Int.is_signed) {
                return irb.CreateSIToFP(src_val, ll_dtype);
            } else {
                return irb.CreateUIToFP(src_val, ll_dtype);
            }
        } else if (src_kind == TYPE_FLOAT) {
            return irb.CreateFPCast(src_val, ll_dtype);
        }
        break;
    case TYPE_BOOL:
        return irb.CreateTrunc(src_val, ll_dtype);
    case TYPE_PTR:
        if (src_kind == TYPE_PTR) {
            return src_val;
        } else if (src_kind == TYPE_INT) {
            return irb.CreateIntToPtr(src_val, ll_dtype);
        }
        break;
    }

    Panic("unimplemented cast in codegen");
    return nullptr;
}

llvm::Value* CodeGenerator::genBinop(AstExpr* node) {
    auto lhs_val = genExpr(node->an_Binop.lhs);

    // Handle short circuit operators.
    if (node->an_Binop.op == AOP_LGAND) {
        auto* start_block = getCurrentBlock(); 
        auto* true_block = appendBlock();
        auto* end_block = appendBlock();

        irb.CreateCondBr(lhs_val, true_block, end_block);

        setCurrentBlock(true_block);
        auto* rhs_val = genExpr(node->an_Binop.rhs);
        irb.CreateBr(end_block);

        setCurrentBlock(end_block);
        auto* phi_node = irb.CreatePHI(llvm::Type::getInt1Ty(ctx), 2);
        phi_node->addIncoming(lhs_val, start_block);
        phi_node->addIncoming(rhs_val, true_block);
        return phi_node;
    } else if (node->an_Binop.op == AOP_LGOR) {
        auto* start_block = getCurrentBlock();
        auto* false_block = appendBlock();
        auto* end_block = appendBlock();

        irb.CreateCondBr(lhs_val, end_block, false_block);

        setCurrentBlock(false_block);
        auto* rhs_val = genExpr(node->an_Binop.rhs);
        irb.CreateBr(end_block);

        setCurrentBlock(end_block);
        auto* phi_node = irb.CreatePHI(llvm::Type::getInt1Ty(ctx), 2);
        phi_node->addIncoming(lhs_val, start_block);
        phi_node->addIncoming(rhs_val, false_block);
        return phi_node;
    }

    auto lhs_type = node->an_Binop.lhs->type->Inner();
    auto* rhs_val = genExpr(node->an_Binop.rhs);
    switch (node->an_Binop.op) {
    case AOP_ADD:
        if (lhs_type->kind == TYPE_INT) {
            return irb.CreateAdd(lhs_val, rhs_val);
        } else {
            Assert(lhs_type->kind == TYPE_FLOAT, "invalid types for ADD op in codegen");
            return irb.CreateFAdd(lhs_val, rhs_val);
        }
        break;
    case AOP_SUB:
        if (lhs_type->kind == TYPE_INT) {
            return irb.CreateSub(lhs_val, rhs_val);
        } else {
            Assert(lhs_type->kind == TYPE_FLOAT, "invalid types for SUB op in codegen");
            return irb.CreateFSub(lhs_val, rhs_val);
        }
        break;
    case AOP_MUL:
        if (lhs_type->kind == TYPE_INT) {
            return irb.CreateMul(lhs_val, rhs_val);
        } else {
            Assert(lhs_type->kind == TYPE_FLOAT, "invalid types for MUL op in codegen");
            return irb.CreateFMul(lhs_val, rhs_val);
        }
        break;
    case AOP_DIV:
        if (lhs_type->kind == TYPE_INT) {
            if (lhs_type->ty_Int.is_signed) {
                return irb.CreateSDiv(lhs_val, rhs_val);
            } else {
                return irb.CreateUDiv(lhs_val, rhs_val);
            }
        } else {
            Assert(lhs_type->kind == TYPE_FLOAT, "invalid types for DIV op in codegen");
            return irb.CreateFDiv(lhs_val, rhs_val);         
        }
        break;
    case AOP_MOD:
        if (lhs_type->kind == TYPE_INT) {
            if (lhs_type->ty_Int.is_signed) {
                return irb.CreateSRem(lhs_val, rhs_val);
            } else {
                return irb.CreateURem(lhs_val, rhs_val);
            }
        } else {
            Assert(lhs_type->kind == TYPE_FLOAT, "invalid types for MOD op in codegen");
            return irb.CreateFRem(lhs_val, rhs_val);
        }
        break;
    case AOP_SHL:
        Assert(lhs_type->kind == TYPE_INT, "invalid types for SHL op in codegen");
        return irb.CreateShl(lhs_val, rhs_val);
    case AOP_SHR:
        Assert(lhs_type->kind == TYPE_INT, "invalid types for SHR op in codegen");

        if (lhs_type->ty_Int.is_signed) {
            return irb.CreateAShr(lhs_val, rhs_val);
        } else {
            return irb.CreateLShr(lhs_val, rhs_val);
        }
    break;
    case AOP_EQ:
        if (lhs_type->kind == TYPE_INT || lhs_type->kind == TYPE_PTR || lhs_type->kind == TYPE_BOOL) {
            return irb.CreateICmpEQ(lhs_val, rhs_val);
        } else {
            Assert(lhs_type->kind == TYPE_FLOAT, "invalid types for EQ op in codegen");
            return irb.CreateFCmpOEQ(lhs_val, rhs_val);
        }
        break;
    case AOP_NE:
        if (lhs_type->kind == TYPE_INT || lhs_type->kind == TYPE_PTR || lhs_type->kind == TYPE_BOOL) {
            return irb.CreateICmpNE(lhs_val, rhs_val);
        } else {
            Assert(lhs_type->kind == TYPE_FLOAT, "invalid types for NE op in codegen");
            return irb.CreateFCmpONE(lhs_val, rhs_val);
        }
        break;
    case AOP_LT:
        if (lhs_type->kind == TYPE_INT) {
            if (lhs_type->ty_Int.is_signed) {
                return irb.CreateICmpSLT(lhs_val, rhs_val);
            } else {
                return irb.CreateICmpULT(lhs_val, rhs_val);
            }
        } else {
            Assert(lhs_type->kind == TYPE_FLOAT, "invalid types for LT op in codegen");
            return irb.CreateFCmpOLT(lhs_val, rhs_val);
        }
        break;
    case AOP_GT:
        if (lhs_type->kind == TYPE_INT) {
            if (lhs_type->ty_Int.is_signed) {
                return irb.CreateICmpSGT(lhs_val, rhs_val);
            } else {
                return irb.CreateICmpUGT(lhs_val, rhs_val);
            }
        } else {
            Assert(lhs_type->kind == TYPE_FLOAT, "invalid types for GT op in codegen");
            return irb.CreateFCmpOGT(lhs_val, rhs_val);
        }
        break;
    case AOP_LE:
        if (lhs_type->kind == TYPE_INT) {
            if (lhs_type->ty_Int.is_signed) {
                return irb.CreateICmpSLE(lhs_val, rhs_val);
            } else {
                return irb.CreateICmpULE(lhs_val, rhs_val);
            }
        } else {
            Assert(lhs_type->kind == TYPE_FLOAT, "invalid types for LE op in codegen");
            return irb.CreateFCmpOLE(lhs_val, rhs_val);
        }
        break;
    case AOP_GE:
        if (lhs_type->kind == TYPE_INT) {
            if (lhs_type->ty_Int.is_signed) {
                return irb.CreateICmpSGE(lhs_val, rhs_val);
            } else {
                return irb.CreateICmpUGE(lhs_val, rhs_val);
            }
        } else {
            Assert(lhs_type->kind == TYPE_FLOAT, "invalid types for GE op in codegen");
            return irb.CreateFCmpOGE(lhs_val, rhs_val);
        }
        break;
    case AOP_BWAND:
        Assert(lhs_type->kind == TYPE_INT, "invalid types for BWAND op in codegen");
        return irb.CreateAnd(lhs_val, rhs_val);
    case AOP_BWOR:
        Assert(lhs_type->kind == TYPE_INT, "invalid types for BWOR op in codegen");
        return irb.CreateOr(lhs_val, rhs_val);
    case AOP_BWXOR:
        Assert(lhs_type->kind == TYPE_INT, "invalid types for BWXOR op in codegen");
        return irb.CreateXor(lhs_val, rhs_val);   
    }

    Panic("unsupported binary operator in codegen: {}", (int)node->an_Binop.op);
}

llvm::Value* CodeGenerator::genUnop(AstExpr* node) {
    auto* operand_type = node->an_Unop.operand->type->Inner();
    auto* operand_val = genExpr(node->an_Unop.operand);
    switch (node->an_Unop.op) {
    case AOP_NEG:
        if (operand_type->kind == TYPE_INT) {
            return irb.CreateNeg(operand_val);
        } else {
            Assert(operand_type->kind == TYPE_FLOAT, "invalid type for NEG in codegen");
            return irb.CreateFNeg(operand_val);
        }
        break;
    case AOP_NOT:
        Assert(operand_type->kind == TYPE_BOOL, "invalid type for NOT in codegen");
        return irb.CreateNot(operand_val);
    }

    Panic("unsupported unary operator in codegen: {}", (int)node->an_Unop.op);
    return nullptr;
}

llvm::Value* CodeGenerator::genCall(AstExpr* node, llvm::Value* alloc_loc) {
    // TODO: smart allocation (using alloc_loc)

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

    return irb.CreateCall(ll_func_type, func_ptr, args);
}

llvm::Value* CodeGenerator::genIndexExpr(AstExpr* node, bool expect_addr) {
    auto& aindex = node->an_Index;

    Assert(aindex.array->type->Inner()->kind == TYPE_ARRAY, "index on non-array type in codegen");

    auto* array_val = genExpr(aindex.array);
    auto* index_val = genExpr(aindex.index);

    genBoundsCheck(index_val, getArrayLen(array_val));

    auto* ll_elem_type = genType(node->type->Inner());
    auto* elem_ptr = irb.CreateGEP(llvm::ArrayType::get(ll_elem_type, 0), getArrayData(array_val), { getInt32Const(0), index_val });

    if (expect_addr) {
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
        irb.CreateCall(ll_panic_func);
        irb.CreateUnreachable();

        setCurrentBlock(bb_is_good_slice);
    }

    auto* ll_elem_type = genType(node->type->ty_Array.elem_type);

    auto* new_arr_data = irb.CreateGEP(llvm::ArrayType::get(ll_elem_type, 0), getArrayData(array_val), start_ndx_val);
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

    if (expect_addr) {
        auto* root_ptr = genExpr(afield.root, true);

        if (root_inner_type->kind == TYPE_ARRAY) {
            if (afield.field_name == "_ptr") {
                return getArrayDataPtr(root_ptr);
            } else if (afield.field_name == "_len") {
                return getArrayLenPtr(root_ptr);
            } else {
                Panic("bad array field {} in codegen", afield.field_name);
            }
        }
    } else {
        auto* root_val = genExpr(afield.root);

        if (root_inner_type->kind == TYPE_ARRAY) {
            if (afield.field_name == "_ptr") {
                return getArrayData(root_val);
            } else if (afield.field_name == "_len") {
                return getArrayLen(root_val);
            } else {
                Panic("bad array field {} in codegen", afield.field_name);
            }
        }
    }
    
    Panic("get field on non-array type in codegen");
    return nullptr;
}
/* -------------------------------------------------------------------------- */

llvm::Value* CodeGenerator::genArrayLit(AstExpr* node, llvm::Value* alloc_loc) {
    auto& array = node->an_Array;

    auto* ll_elem_type = genType(array.elems[0]->type);
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
        
        auto* elem_val = genExpr(array.elems[i]);
        irb.CreateStore(elem_val, elem_ptr);
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
    
    auto* ll_elem_type = genType(anew.elem_type);
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
        if (anew.alloc_mode == A_ALLOC_HEAP) {
            Panic("heap allocation not implemented in codegen");
        } else if (anew.alloc_mode == A_ALLOC_GLOBAL) {
            addr_val = new llvm::GlobalVariable(
                mod,
                ll_elem_type,
                false,
                llvm::GlobalValue::PrivateLinkage,
                getNullValue(ll_elem_type)
            );
        } else {
            addr_val = irb.CreateAlloca(ll_elem_type);
            irb.CreateStore(getNullValue(ll_elem_type), addr_val);
        }

        return addr_val;
    }
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

        llvm::Value* str_array = getNullValue(ll_array_type);
        str_array = irb.CreateInsertValue(str_array, gv_str, 0);
        return irb.CreateInsertValue(str_array, len_const, 1);
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
    irb.CreateCall(ll_panic_func);
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

llvm::Constant* CodeGenerator::getPlatformIntConst(uint64_t value) {
    // TODO: 32-bit systems...
    llvm::APInt ap_int(64, value, false);
    return llvm::Constant::getIntegerValue(llvm::Type::getInt64Ty(ctx), ap_int);
}

llvm::Value* CodeGenerator::makeLLVMIntLit(Type* int_type, uint64_t value) {
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

llvm::Value* CodeGenerator::makeLLVMFloatLit(Type* float_type, double value) {
    Assert(float_type->kind == TYPE_FLOAT, "invalid type {} to make a float literal in codegen", float_type->ToString());

    if (float_type->ty_Float.bit_size == 64) {
        llvm::APFloat ap_float(value);
        return llvm::ConstantFP::get(ctx, ap_float);
    } else {
        llvm::APFloat ap_float((float)value);
        return llvm::ConstantFP::get(ctx, ap_float);
    }
}
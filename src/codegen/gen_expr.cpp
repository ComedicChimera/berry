#include "codegen.hpp"


void CodeGenerator::Visit(AstCast& node) {
    visitNode(node.src);

    if (tctx.Equal(node.src->type, node.type)) {
        node.llvm_value = node.src->llvm_value;
        return;
    }

    auto* src_type = node.src->type->Inner();
    auto* dest_type = node.type->Inner();

    auto src_kind = src_type->GetKind();
    auto dest_kind = dest_type->GetKind();

    auto* src_val = node.src->llvm_value;
    auto* ll_dtype = genType(dest_type);

    llvm::Value* val = nullptr;
    switch (dest_kind) {
    case TYPE_INT: {
        auto dest_int_type = dynamic_cast<IntType*>(dest_type);

        if (src_kind == TYPE_INT) {
            auto src_int_type = dynamic_cast<IntType*>(src_type);

            val = builder.CreateIntCast(src_val, ll_dtype, src_int_type->is_signed);
        } else if (src_kind == TYPE_FLOAT) {
            if (dest_int_type->is_signed) {
                val = builder.CreateFPToSI(src_val, ll_dtype);
            } else {
                val = builder.CreateFPToUI(src_val, ll_dtype);
            }
        } else if (src_kind == TYPE_BOOL) {
            val = builder.CreateZExt(src_val, ll_dtype);
        } else if (src_kind == TYPE_PTR) {
            val = builder.CreatePtrToInt(src_val, ll_dtype);
        }
    } break; 
    case TYPE_FLOAT:
        if (src_kind == TYPE_INT) {
            auto* src_int_type = dynamic_cast<IntType*>(src_type);

            if (src_int_type->is_signed) {
                val = builder.CreateSIToFP(src_val, ll_dtype);
            } else {
                val = builder.CreateUIToFP(src_val, ll_dtype);
            }
        } else if (src_kind == TYPE_FLOAT) {
            val = builder.CreateFPCast(src_val, ll_dtype);
        }
        break;
    case TYPE_BOOL:
        val = builder.CreateTrunc(src_val, ll_dtype);
        break;
    case TYPE_PTR:
        if (src_kind == TYPE_PTR) {
            val = src_val;
        } else if (src_kind == TYPE_INT) {
            val = builder.CreateIntToPtr(src_val, ll_dtype);
        }
        break;
    }
    if (val == nullptr) {
        Panic("unimplemented cast in codegen");
    }

    node.llvm_value = val;
}

void CodeGenerator::Visit(AstBinaryOp& node) {
    visitNode(node.lhs);

    // Handle short circuit operators.
    if (node.op_kind == AOP_LGAND) {
        auto* start_block = getCurrentBlock(); 
        auto* true_block = appendBlock();
        auto* end_block = appendBlock();

        builder.CreateCondBr(node.lhs->llvm_value, true_block, end_block);

        setCurrentBlock(true_block);
        visitNode(node.rhs);
        builder.CreateBr(end_block);

        setCurrentBlock(end_block);
        auto* phi_node = builder.CreatePHI(llvm::Type::getInt1Ty(ctx), 2);
        phi_node->addIncoming(node.lhs->llvm_value, start_block);
        phi_node->addIncoming(node.rhs->llvm_value, true_block);

        node.llvm_value = phi_node;
        return;
    } else if (node.op_kind == AOP_LGOR) {
        auto* start_block = getCurrentBlock();
        auto* false_block = appendBlock();
        auto* end_block = appendBlock();

        builder.CreateCondBr(node.lhs->llvm_value, end_block, false_block);

        setCurrentBlock(false_block);
        visitNode(node.rhs);
        builder.CreateBr(end_block);

        setCurrentBlock(end_block);
        auto* phi_node = builder.CreatePHI(llvm::Type::getInt1Ty(ctx), 2);
        phi_node->addIncoming(node.lhs->llvm_value, start_block);
        phi_node->addIncoming(node.rhs->llvm_value, false_block);

        node.llvm_value = phi_node;
        return;
    }

    visitNode(node.rhs);

    auto lhs_type = node.lhs->type->Inner();
    auto* lhs_val = node.lhs->llvm_value;
    auto* rhs_val = node.rhs->llvm_value;
    switch (node.op_kind) {
    case AOP_ADD:
        if (lhs_type->GetKind() == TYPE_INT) {
            node.llvm_value = builder.CreateAdd(lhs_val, rhs_val);
        } else {
            Assert(lhs_type->GetKind() == TYPE_FLOAT, "invalid types for ADD op in codegen");
            node.llvm_value = builder.CreateFAdd(lhs_val, rhs_val);
        }
        return;
    case AOP_SUB:
        if (lhs_type->GetKind() == TYPE_INT) {
            node.llvm_value = builder.CreateSub(lhs_val, rhs_val);
        } else {
            Assert(lhs_type->GetKind() == TYPE_FLOAT, "invalid types for SUB op in codegen");
            node.llvm_value = builder.CreateFSub(lhs_val, rhs_val);
        }
        return;
    case AOP_MUL:
        if (lhs_type->GetKind() == TYPE_INT) {
            node.llvm_value = builder.CreateMul(lhs_val, rhs_val);
        } else {
            Assert(lhs_type->GetKind() == TYPE_FLOAT, "invalid types for MUL op in codegen");
            node.llvm_value = builder.CreateFMul(lhs_val, rhs_val);
        }
        return;
    case AOP_DIV:
        if (lhs_type->GetKind() == TYPE_INT) {
            auto* int_type = dynamic_cast<IntType*>(lhs_type);
            if (int_type->is_signed) {
                node.llvm_value = builder.CreateSDiv(lhs_val, rhs_val);
            } else {
                node.llvm_value = builder.CreateUDiv(lhs_val, rhs_val);
            }
        } else {
            Assert(lhs_type->GetKind() == TYPE_FLOAT, "invalid types for DIV op in codegen");
            node.llvm_value = builder.CreateFDiv(lhs_val, rhs_val);         
        }
        return;
    case AOP_MOD:
        if (lhs_type->GetKind() == TYPE_INT) {
            auto* int_type = dynamic_cast<IntType*>(lhs_type);
            if (int_type->is_signed) {
                node.llvm_value = builder.CreateSRem(lhs_val, rhs_val);
            } else {
                node.llvm_value = builder.CreateURem(lhs_val, rhs_val);
            }
        } else {
            Assert(lhs_type->GetKind() == TYPE_FLOAT, "invalid types for MOD op in codegen");
            node.llvm_value = builder.CreateFRem(lhs_val, rhs_val);
        }
        return;
    case AOP_SHL:
        Assert(lhs_type->GetKind() == TYPE_INT, "invalid types for SHL op in codegen");
        node.llvm_value = builder.CreateShl(lhs_val, rhs_val);
        return;
    case AOP_SHR: {
        Assert(lhs_type->GetKind() == TYPE_INT, "invalid types for SHR op in codegen");

        auto* int_type = dynamic_cast<IntType*>(lhs_type);

        if (int_type->is_signed) {
            node.llvm_value = builder.CreateAShr(lhs_val, rhs_val);
        } else {
            node.llvm_value = builder.CreateLShr(lhs_val, rhs_val);
        }
    } return;
    case AOP_EQ:
        if (lhs_type->GetKind() == TYPE_INT || lhs_type->GetKind() == TYPE_PTR || lhs_type->GetKind() == TYPE_BOOL) {
            node.llvm_value = builder.CreateICmpEQ(lhs_val, rhs_val);
        } else {
            Assert(lhs_type->GetKind() == TYPE_FLOAT, "invalid types for EQ op in codegen");
            node.llvm_value = builder.CreateFCmpOEQ(lhs_val, rhs_val);
        }
        return;
    case AOP_NE:
        if (lhs_type->GetKind() == TYPE_INT || lhs_type->GetKind() == TYPE_PTR || lhs_type->GetKind() == TYPE_BOOL) {
            node.llvm_value = builder.CreateICmpNE(lhs_val, rhs_val);
        } else {
            Assert(lhs_type->GetKind() == TYPE_FLOAT, "invalid types for NE op in codegen");
            node.llvm_value = builder.CreateFCmpONE(lhs_val, rhs_val);
        }
        return;
    case AOP_LT:
        if (lhs_type->GetKind() == TYPE_INT) {
            auto* int_type = dynamic_cast<IntType*>(lhs_type);

            if (int_type->is_signed) {
                node.llvm_value = builder.CreateICmpSLT(lhs_val, rhs_val);
            } else {
                node.llvm_value = builder.CreateICmpULT(lhs_val, rhs_val);
            }
        } else {
            Assert(lhs_type->GetKind() == TYPE_FLOAT, "invalid types for LT op in codegen");
            node.llvm_value = builder.CreateFCmpOLT(lhs_val, rhs_val);
        }
        return;
    case AOP_GT:
        if (lhs_type->GetKind() == TYPE_INT) {
            auto* int_type = dynamic_cast<IntType*>(lhs_type);

            if (int_type->is_signed) {
                node.llvm_value = builder.CreateICmpSGT(lhs_val, rhs_val);
            } else {
                node.llvm_value = builder.CreateICmpUGT(lhs_val, rhs_val);
            }
        } else {
            Assert(lhs_type->GetKind() == TYPE_FLOAT, "invalid types for GT op in codegen");
            node.llvm_value = builder.CreateFCmpOGT(lhs_val, rhs_val);
        }
        return;
    case AOP_LE:
        if (lhs_type->GetKind() == TYPE_INT) {
            auto* int_type = dynamic_cast<IntType*>(lhs_type);

            if (int_type->is_signed) {
                node.llvm_value = builder.CreateICmpSLE(lhs_val, rhs_val);
            } else {
                node.llvm_value = builder.CreateICmpULE(lhs_val, rhs_val);
            }
        } else {
            Assert(lhs_type->GetKind() == TYPE_FLOAT, "invalid types for LE op in codegen");
            node.llvm_value = builder.CreateFCmpOLE(lhs_val, rhs_val);
        }
        return;
    case AOP_GE:
        if (lhs_type->GetKind() == TYPE_INT) {
            auto* int_type = dynamic_cast<IntType*>(lhs_type);

            if (int_type->is_signed) {
                node.llvm_value = builder.CreateICmpSGE(lhs_val, rhs_val);
            } else {
                node.llvm_value = builder.CreateICmpUGE(lhs_val, rhs_val);
            }
        } else {
            Assert(lhs_type->GetKind() == TYPE_FLOAT, "invalid types for GE op in codegen");
            node.llvm_value = builder.CreateFCmpOGE(lhs_val, rhs_val);
        }
        return;
    case AOP_BWAND:
        Assert(lhs_type->GetKind() == TYPE_INT, "invalid types for BWAND op in codegen");
        node.llvm_value = builder.CreateAnd(lhs_val, rhs_val);
        return;
    case AOP_BWOR:
        Assert(lhs_type->GetKind() == TYPE_INT, "invalid types for BWOR op in codegen");
        node.llvm_value = builder.CreateOr(lhs_val, rhs_val);
        return;
    case AOP_BWXOR:
        Assert(lhs_type->GetKind() == TYPE_INT, "invalid types for BWXOR op in codegen");
        node.llvm_value = builder.CreateXor(lhs_val, rhs_val);
        return;     
    }

    Panic("unsupported binary operator in codegen: {}", (int)node.op_kind);
}

void CodeGenerator::Visit(AstUnaryOp& node) {
    visitNode(node.operand);

    auto* operand_type = node.type->Inner();
    auto* operand_val = node.operand->llvm_value;
    switch (node.op_kind) {
    case AOP_NEG:
        if (operand_type->GetKind() == TYPE_INT) {
            node.llvm_value = builder.CreateNeg(operand_val);
        } else {
            Assert(operand_type->GetKind() == TYPE_FLOAT, "invalid type for NEG in codegen");
            node.llvm_value = builder.CreateFNeg(operand_val);
        }
        return;
    case AOP_NOT:
        Assert(operand_type->GetKind() == TYPE_BOOL, "invalid type for NOT in codegen");
        node.llvm_value = builder.CreateNot(operand_val);
        return;
    }

    Panic("unsupported unary operator in codegen: {}", (int)node.op_kind);
}

void CodeGenerator::Visit(AstAddrOf& node) {
    Assert(node.elem->IsLValue(), "tried to take address of r-value in codegen");

    pushValueMode(false);
    visitNode(node.elem);
    popValueMode();

    node.llvm_value = node.elem->llvm_value;
}

void CodeGenerator::Visit(AstDeref& node) {
    visitNode(node.ptr);
    
    if (isValueMode()) {
        node.llvm_value = node.ptr->llvm_value;
    } else {
        node.llvm_value = builder.CreateLoad(genType(node.ptr->type), node.ptr->llvm_value);
    }
}

/* -------------------------------------------------------------------------- */

void CodeGenerator::Visit(AstCall& node) {
    visitNode(node.func);

    // Handle global values.
    llvm::FunctionType* ll_func_type;
    if (node.func->llvm_value->getType()->isPointerTy()) {
        Assert(llvm::Function::classof(node.func->llvm_value), "call to a non-function type in codegen");
        auto* ll_func = llvm::dyn_cast<llvm::Function>(node.func->llvm_value);

        ll_func_type = ll_func->getFunctionType();
    } else {
        Assert(node.func->llvm_value->getType()->isFunctionTy(), "call to a non function type in codegen");

        ll_func_type = llvm::dyn_cast<llvm::FunctionType>(node.func->llvm_value->getType());
    }

    std::vector<llvm::Value*> args;
    for (auto& arg : node.args) {
        visitNode(arg);
        args.push_back(arg->llvm_value);
    }

    node.llvm_value = builder.CreateCall(ll_func_type, node.func->llvm_value, args);
}

llvm::Value* CodeGenerator::getArrayPtr(llvm::Value* array) {
    return builder.CreateExtractValue(array, 0);
}

llvm::Value* CodeGenerator::getArrayLen(llvm::Value* array) {
    return builder.CreateExtractValue(array, 1);
}

void CodeGenerator::genBoundsCheck(llvm::Value* ndx, llvm::Value* arr_len, bool can_equal_len) {
    auto* is_ge_zero = builder.CreateICmpSGE(ndx, llvm::Constant::getNullValue(ndx->getType()));
    llvm::Value* is_lt_len;
    if (can_equal_len) {
        is_lt_len = builder.CreateICmpSLE(ndx, arr_len);
    } else {
        is_lt_len = builder.CreateICmpSLT(ndx, arr_len);
    }
    auto* is_in_bounds = builder.CreateAnd(is_ge_zero, is_lt_len);

    auto* bb_oob = appendBlock();
    auto* bb_in_bounds = appendBlock();

    builder.CreateCondBr(is_in_bounds, bb_in_bounds, bb_oob);
    
    setCurrentBlock(bb_oob);
    builder.CreateCall(ll_panic_func);
    builder.CreateUnreachable();

    setCurrentBlock(bb_in_bounds);
}

void CodeGenerator::Visit(AstIndex& node) {
    Assert(node.array->type->Inner()->GetKind() == TYPE_ARRAY, "index on non-array type in codegen");

    visitNode(node.array);
    visitNode(node.index);

    genBoundsCheck(node.index->llvm_value, getArrayLen(node.array->llvm_value));

    auto* array_type = dynamic_cast<ArrayType*>(node.array->type->Inner());
    auto* elem_type = genType(array_type->elem_type);
    auto* elem_ptr = builder.CreateGEP(llvm::ArrayType::get(elem_type, 0), getArrayPtr(node.array->llvm_value), node.index->llvm_value);

    if (isValueMode()) {
        node.llvm_value = builder.CreateLoad(elem_type, elem_ptr);
    } else {
        node.llvm_value = elem_ptr;
    }
}

void CodeGenerator::Visit(AstSlice& node) {
    Assert(isValueMode(), "slice assignment is not implemented");

    Assert(node.array->type->Inner()->GetKind() == TYPE_ARRAY, "slice on non-array type in codegen");

    visitNode(node.array);
    auto* arr_len = getArrayLen(node.array->llvm_value);

    bool needs_bad_slice_check = node.start_index && node.end_index;

    llvm::Value* start_ndx;
    if (node.start_index) {
        visitNode(node.start_index);
        genBoundsCheck(node.start_index->llvm_value, arr_len);
        start_ndx = node.start_index->llvm_value;
    } else {
        start_ndx = llvm::Constant::getNullValue(llvm::Type::getInt64Ty(ctx));
    }

    llvm::Value* end_ndx;
    if (node.end_index) {
        visitNode(node.end_index);
        genBoundsCheck(node.end_index->llvm_value, arr_len, true);
        end_ndx = node.end_index->llvm_value;
    } else {
        end_ndx = arr_len;
    }

    if (needs_bad_slice_check) {
        auto* is_good_slice = builder.CreateICmpSLE(start_ndx, end_ndx);

        auto* bb_is_bad_slice = appendBlock();
        auto* bb_is_good_slice = appendBlock();

        builder.CreateCondBr(is_good_slice, bb_is_good_slice, bb_is_bad_slice);

        setCurrentBlock(bb_is_bad_slice);
        builder.CreateCall(ll_panic_func);
        builder.CreateUnreachable();

        setCurrentBlock(bb_is_good_slice);
    }

    auto* array_type = dynamic_cast<ArrayType*>(node.array->type->Inner());
    auto* elem_type = genType(array_type->elem_type);

    auto* new_arr_ptr = builder.CreateGEP(llvm::ArrayType::get(elem_type, 0), getArrayPtr(node.array->llvm_value), start_ndx);
    auto* new_arr_len = builder.CreateSub(end_ndx, start_ndx);

    auto* empty_array = llvm::Constant::getNullValue(ll_array_type);
    auto* new_array = builder.CreateInsertValue(empty_array, new_arr_ptr, 0);
    node.llvm_value = builder.CreateInsertValue(new_array, new_arr_len, 1);
}

void CodeGenerator::Visit(AstFieldAccess& node) {
    Assert(isValueMode(), "field assignment is not implemented");

    visitNode(node.root);

    if (node.root->type->Inner()->GetKind() == TYPE_ARRAY) {
        if (node.field_name == "_ptr") {
            node.llvm_value = getArrayPtr(node.root->llvm_value);
        } else if (node.field_name == "_len") {
            node.llvm_value = getArrayLen(node.root->llvm_value);
        } else {
            Panic("bad array field {} in codegen", node.field_name);
        }
    } else {
        Panic("get field on non-array type in codegen");
    }
}

/* -------------------------------------------------------------------------- */

void CodeGenerator::Visit(AstArrayLit& node) {
    // TODO: how to handle variables?
    // let x = [1, 2, 3]?
}

/* -------------------------------------------------------------------------- */

void CodeGenerator::Visit(AstStringLit& node) {
    auto* str_constant = llvm::ConstantDataArray::getString(ctx, node.value, false);

    auto* gv_str_data = new llvm::GlobalVariable(
        mod,
        str_constant->getType(),
        true,
        llvm::GlobalValue::PrivateLinkage,
        str_constant
    );
    gv_str_data->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
    
    llvm::APInt ap_len(64, node.value.size(), false);
    auto* str_array_constant = llvm::ConstantStruct::get(
        ll_array_type,
        {
            gv_str_data,
            llvm::Constant::getIntegerValue(llvm::Type::getInt64Ty(ctx), ap_len)
        }
    );

    auto* gv_str = new llvm::GlobalVariable(
        mod,
        ll_array_type,
        true,
        llvm::GlobalValue::PrivateLinkage,
        str_array_constant
    );
    gv_str->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

    node.llvm_value = gv_str;
}

void CodeGenerator::Visit(AstNullLit& node) {
    node.llvm_value = llvm::Constant::getNullValue(genType(node.type));
}

void CodeGenerator::Visit(AstIdent& node) {
    if (node.symbol->kind == SYM_VARIABLE && isValueMode()) {
        auto* ll_value = builder.CreateLoad(genType(node.type), node.symbol->llvm_value);
        node.llvm_value = ll_value;
        return;
    }

    node.llvm_value = node.symbol->llvm_value;
}

void CodeGenerator::Visit(AstIntLit& node) {
    // NOTE: It is possible for an int literal to have a float type if a number
    // literal implicitly takes on a floating point value.
    auto inner_type = node.type->Inner();
    switch (inner_type->GetKind()) {
    case TYPE_INT: {
        auto* int_type = dynamic_cast<IntType*>(inner_type);
        llvm::APInt ap_int(int_type->bit_size, node.value, int_type->is_signed);
        node.llvm_value = llvm::Constant::getIntegerValue(genType(node.type), ap_int);
    } break;
    case TYPE_FLOAT: {
        auto* float_type = dynamic_cast<FloatType*>(inner_type);
        node.llvm_value = makeLLVMFloatLit(float_type, (double)node.value);
    } break;
    default:
        Panic("non-numeric type integer literal in codegen");
    }    
}

void CodeGenerator::Visit(AstFloatLit& node) {
    Assert(node.type->Inner()->GetKind() == TYPE_FLOAT, "non-floating type float literal in codegen");
    auto* float_type = dynamic_cast<FloatType*>(node.type->Inner());

    node.llvm_value = makeLLVMFloatLit(float_type, node.value);
}

llvm::Value* CodeGenerator::makeLLVMFloatLit(FloatType* float_type, double value) {
    if (float_type->bit_size == 64) {
        llvm::APFloat ap_float(value);
        return llvm::ConstantFP::get(ctx, ap_float);
    } else {
        llvm::APFloat ap_float((float)value);
        return llvm::ConstantFP::get(ctx, ap_float);
    }
}

void CodeGenerator::Visit(AstBoolLit& node) {
    llvm::APInt ap_int(1, (uint64_t)node.value, false);

    node.llvm_value = llvm::Constant::getIntegerValue(llvm::Type::getInt1Ty(ctx), ap_int);
}
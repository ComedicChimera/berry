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

/* -------------------------------------------------------------------------- */

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
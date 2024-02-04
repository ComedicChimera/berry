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
    Panic("codegen for binary ops is not implemented yet");
}

void CodeGenerator::Visit(AstUnaryOp& node) {
    visitNode(node.operand);

    switch (node.op_kind) {
    case AOP_NEG: {
        if (node.type->Inner()->GetKind() == TYPE_INT) {
            node.llvm_value = builder.CreateNeg(node.operand->llvm_value);
        } else if (node.type->Inner()->GetKind() == TYPE_FLOAT) {
            node.llvm_value = builder.CreateFNeg(node.operand->llvm_value);
        } else {
            Panic("invalid operand for unary `-` in codegen");
        }
    } break;
    }
}

void CodeGenerator::Visit(AstAddrOf& node) {
    // TODO: handle heap allocations
    Assert(node.elem->IsLValue(), "referencing to rvalues is not implemented yet");

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
    Assert(node.type->Inner()->GetKind() == TYPE_INT, "non-integral type integer literal in codegen");
    auto* int_type = dynamic_cast<IntType*>(node.type->Inner());

    llvm::APInt ap_int(int_type->bit_size, node.value, int_type->is_signed);
    node.llvm_value = llvm::Constant::getIntegerValue(genType(node.type), ap_int);
}

void CodeGenerator::Visit(AstFloatLit& node) {
    Assert(node.type->Inner()->GetKind() == TYPE_FLOAT, "non-floating type float literal in codegen");
    auto* float_type = dynamic_cast<FloatType*>(node.type->Inner());

    if (float_type->bit_size == 64) {
        llvm::APFloat ap_float(node.value);
        node.llvm_value = llvm::ConstantFP::get(ctx, ap_float);
    } else {
        llvm::APFloat ap_float((float)node.value);
        node.llvm_value = llvm::ConstantFP::get(ctx, ap_float);
    }
}

void CodeGenerator::Visit(AstBoolLit& node) {
    llvm::APInt ap_int(1, (uint64_t)node.value, false);

    node.llvm_value = llvm::Constant::getIntegerValue(llvm::Type::getInt1Ty(ctx), ap_int);
}
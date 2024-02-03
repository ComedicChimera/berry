#include "codegen.hpp"


void CodeGenerator::Visit(AstAddrOf& node) {
    // TODO: handle heap allocations
    Assert(node.elem->IsLValue(), "referencing to rvalues is not implemented yet");

    pushValueMode(false);
    visitNode(node.elem);
    popValueMode();

    node.llvm_value = node.elem->llvm_value;
}

/* -------------------------------------------------------------------------- */

void CodeGenerator::Visit(AstCall& node) {
    visitNode(node.func);

    // Handle global values.
    llvm::FunctionType* ll_func_type;
    if (node.func->llvm_value->getType()->isPointerTy()) {
        Assert(llvm::Function::classof(node.func->llvm_value), "call to a non-function type in code generator");
        auto* ll_func = llvm::dyn_cast<llvm::Function>(node.func->llvm_value);

        ll_func_type = ll_func->getFunctionType();
    } else {
        Assert(node.func->llvm_value->getType()->isFunctionTy(), "call to a non function type in code generator");

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
    if (node.symbol->kind == SYM_VALUE && isValueMode()) {
        auto* ll_value = builder.CreateLoad(genType(node.type), node.symbol->llvm_value);
        node.llvm_value = ll_value;
        return;
    }

    node.llvm_value = node.symbol->llvm_value;
}

void CodeGenerator::Visit(AstIntLit& node) {
    Assert(node.type->Inner()->GetKind() == TYPE_INT, "non-integral type integer literal");
    auto* int_type = dynamic_cast<IntType*>(node.type->Inner());

    llvm::APInt ap_int(int_type->bit_size, node.value, int_type->is_signed);
    node.llvm_value = llvm::Constant::getIntegerValue(genType(node.type), ap_int);
}

void CodeGenerator::Visit(AstFloatLit& node) {
    Assert(node.type->Inner()->GetKind() == TYPE_FLOAT, "non-floating type float literal");
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
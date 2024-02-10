#include "codegen.hpp"

#include <iostream>

#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"

std::unordered_map<std::string, llvm::CallingConv::ID> cconv_name_to_id {
    { "c", llvm::CallingConv::C },
    { "stdcall", llvm::CallingConv::X86_StdCall },
    { "win64", llvm::CallingConv::Win64 }
};

void CodeGenerator::Visit(AstFuncDef& node) {
    if (pred_mode) {
        if (node.body) {
            debug.BeginFuncBody(node, llvm::dyn_cast<llvm::Function>(node.symbol->llvm_value));

            genFuncBody(node);

            debug.EndFuncBody();
        }

        return;
    }

    auto* ll_type = genType(node.symbol->type);
    Assert(ll_type->isFunctionTy(), "function signature is not a function type in codegen");

    auto* ll_func_type = llvm::dyn_cast<llvm::FunctionType>(ll_type);

    bool should_mangle = true;
    bool exported = false;
    llvm::CallingConv::ID cconv = llvm::CallingConv::C;
    for (auto& tag : node.metadata) {
        if (tag.first == "extern" || tag.first == "abientry") {
            exported = true;
            should_mangle = false;
        } else if (tag.first == "callconv") {
            cconv = cconv_name_to_id[tag.second.value];
        }
    }

    std::string ll_name { should_mangle ? mangleName(node.symbol->name) : node.symbol->name };

    llvm::Function* ll_func = llvm::Function::Create(
        ll_func_type, 
        exported ? llvm::Function::ExternalLinkage : llvm::Function::PrivateLinkage,
        ll_name,
        mod
    );

    ll_func->setCallingConv(cconv);

    int i = 0;
    for (auto& arg : ll_func->args()) {
        arg.setName(node.params[i]->name);
        node.params[i]->llvm_value = &arg;
        i++;
    }

    node.symbol->llvm_value = ll_func;
}

void CodeGenerator::genFuncBody(AstFuncDef& node) {
    debug.ClearDebugLocation();

    Assert(llvm::Function::classof(node.symbol->llvm_value), "function def is not a function value in codegen");
    auto* ll_func = llvm::dyn_cast<llvm::Function>(node.symbol->llvm_value);

    var_block = llvm::BasicBlock::Create(ctx, "entry", ll_func);
    setCurrentBlock(var_block);

    for (auto* param : node.params) {
        auto* ll_param = builder.CreateAlloca(param->llvm_value->getType());
        builder.CreateStore(param->llvm_value, ll_param);
        param->llvm_value = ll_param;
    }

    ll_enclosing_func = ll_func;

    auto* body_block = appendBlock();
    setCurrentBlock(body_block);
    
    visitNode(node.body);
    if (!currentHasTerminator()) {
        builder.CreateRetVoid();
    }
    
    ll_enclosing_func = nullptr;

    debug.ClearDebugLocation();
    setCurrentBlock(var_block);
    builder.CreateBr(body_block);

    std::string err_msg;
    llvm::raw_string_ostream oss(err_msg);
    if (llvm::verifyFunction(*ll_func, &oss)) {
        std::cerr << "error: verifying LLVM function:\n\n";
        std::cerr << err_msg << "\n\n";
        std::cerr << "printing module:\n\n";
        mod.print(llvm::errs(), nullptr);
        exit(1);
    }
}

std::string CodeGenerator::mangleName(std::string_view name) {
    return std::format("bry{}.{}.{}", bry_mod.id, bry_mod.name, name);
}

/* -------------------------------------------------------------------------- */

void CodeGenerator::Visit(AstGlobalVarDef &node) {
    if (pred_mode) {
        if (node.var_def->init) {
            // No debug info for global variable expressions.
            debug.PushDisable();

            auto& last_init_block = ll_init_func->back();
            setCurrentBlock(&last_init_block);

            ll_enclosing_func = ll_init_func;
            visitNode(node.var_def->init);
            ll_enclosing_func = nullptr;
            
            builder.CreateStore(node.var_def->init->llvm_value, node.var_def->symbol->llvm_value);

            debug.PopDisable();
        }

        return;
    }

    auto* ll_type = genType(node.var_def->symbol->type);

    auto gv = new llvm::GlobalVariable(
        mod, 
        ll_type, 
        false, 
        llvm::GlobalValue::PrivateLinkage, 
        llvm::Constant::getNullValue(ll_type), 
        mangleName(node.var_def->symbol->name)
    );
    debug.EmitGlobalVariableInfo(node, gv);

    node.var_def->symbol->llvm_value = gv;
}

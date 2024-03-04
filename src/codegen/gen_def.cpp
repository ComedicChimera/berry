#include "codegen.hpp"

#include <iostream>

#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"

void CodeGenerator::genTopDecl(AstDef* def) {
    switch (def->kind) {
    case AST_FUNC:
        genFuncProto(def);
        break;
    case AST_GLOBAL_VAR:
        genGlobalVarDecl(def);
        break;
    case AST_STRUCT_DEF:
        // TODO: handle struct metadata
        genType(def->an_StructDef.symbol->type);
        break;
    default:
        Panic("top declaration codegen not implemented for {}", (int)def->kind);
    }
}

void CodeGenerator::genPredicates(AstDef* def) {
    switch (def->kind) {
    case AST_FUNC:
        if (def->an_Func.body) {
            genFuncBody(def);
        }
        break;
    case AST_GLOBAL_VAR:
        genGlobalVarInit(def);
        break;
    case AST_STRUCT_DEF:
        // Nothing to do here :)
        break;
    default:
        Panic("predicate codegen not implemented for {}", (int)def->kind);
    }
}

/* -------------------------------------------------------------------------- */

void CodeGenerator::genFuncProto(AstDef* node) {
    auto* symbol = node->an_Func.symbol;

    auto* ll_type = genType(symbol->type);
    Assert(ll_type->isFunctionTy(), "function signature is not a function type in codegen");

    auto* ll_func_type = llvm::dyn_cast<llvm::FunctionType>(ll_type);

    bool should_mangle = true;
    bool exported = symbol->export_num != UNEXPORTED;
    llvm::CallingConv::ID cconv = llvm::CallingConv::C;
    for (auto& tag : node->metadata) {
        if (tag.name == "extern" || tag.name == "abientry") {
            exported = true;
            should_mangle = false;
        } else if (tag.name == "callconv") {
            cconv = cconv_name_to_id[tag.value];
        }
    }

    std::string ll_name { should_mangle ? mangleName(symbol->name) : symbol->name };

    llvm::Function* ll_func = llvm::Function::Create(
        ll_func_type, 
        exported ? llvm::Function::ExternalLinkage : llvm::Function::PrivateLinkage,
        ll_name,
        mod
    );

    ll_func->setCallingConv(cconv);

    size_t offset = ll_func->arg_size() > node->an_Func.params.size();
    for (size_t i = 0; i < node->an_Func.params.size(); i++) {
        auto arg = ll_func->getArg(i + offset);

        arg->setName(node->an_Func.params[i]->name);
        node->an_Func.params[i]->llvm_value = arg;
    }

    symbol->llvm_value = ll_func;
}

void CodeGenerator::genFuncBody(AstDef* node) {
    debug.BeginFuncBody(node, llvm::dyn_cast<llvm::Function>(node->an_Func.symbol->llvm_value));
    debug.ClearDebugLocation();

    Assert(llvm::Function::classof(node->an_Func.symbol->llvm_value), "function def is not a function value in codegen");
    auto* ll_func = llvm::dyn_cast<llvm::Function>(node->an_Func.symbol->llvm_value);

    var_block = llvm::BasicBlock::Create(ctx, "entry", ll_func);
    setCurrentBlock(var_block);

    for (auto* param : node->an_Func.params) {
        auto* ll_type = genType(param->type, true);
        auto* ll_param = irb.CreateAlloca(ll_type);

        if (shouldPtrWrap(ll_type)) {
            genStructCopy(ll_type, param->llvm_value, ll_param);
        } else {
            irb.CreateStore(param->llvm_value, ll_param);
        }
        
        param->llvm_value = ll_param;
    }

    if (ll_func->arg_size() > node->an_Func.params.size()) {
        return_param = &(*ll_func->arg_begin());
    } else {
        return_param = nullptr;
    }

    ll_enclosing_func = ll_func;

    auto* body_block = appendBlock();
    setCurrentBlock(body_block);
    
    genStmt(node->an_Func.body);
    if (!currentHasTerminator()) {
        irb.CreateRetVoid();
    }
    
    ll_enclosing_func = nullptr;

    debug.ClearDebugLocation();
    setCurrentBlock(var_block);
    irb.CreateBr(body_block);

    std::string err_msg;
    llvm::raw_string_ostream oss(err_msg);
    if (llvm::verifyFunction(*ll_func, &oss)) {
        std::cerr << "error: verifying LLVM function:\n\n";
        std::cerr << err_msg << "\n\n";
        std::cerr << "printing module:\n\n";
        mod.print(llvm::errs(), nullptr);
        exit(1);
    }

    debug.EndFuncBody();
}

/* -------------------------------------------------------------------------- */

void CodeGenerator::genGlobalVarDecl(AstDef* node) {
    auto* symbol = node->an_GlobalVar.symbol;

    auto* ll_type = genType(symbol->type);

    // TODO: handle metadata
    Assert(node->metadata.size() == 0, "metadata for global variables not implemented");
    
    bool exported = symbol->export_num != UNEXPORTED;
    auto gv = new llvm::GlobalVariable(
        mod, 
        ll_type, 
        false, 
        exported ? llvm::GlobalValue::ExternalLinkage : llvm::GlobalValue::PrivateLinkage, 
        llvm::Constant::getNullValue(ll_type), 
        mangleName(symbol->name)
    );
    debug.EmitGlobalVariableInfo(node, gv);

    symbol->llvm_value = gv;
}

void CodeGenerator::genGlobalVarInit(AstDef* node) {
    if (!node->an_GlobalVar.init) {
        return;
    }

    // No debug info for global variable expressions.
    debug.PushDisable();

    setCurrentBlock(ll_init_block);

    ll_enclosing_func = ll_init_func;
    genStoreExpr(node->an_GlobalVar.init, node->an_GlobalVar.symbol->llvm_value);
    ll_enclosing_func = nullptr;
    
    ll_init_block = getCurrentBlock();

    debug.PopDisable();
}

/* -------------------------------------------------------------------------- */

std::string CodeGenerator::mangleName(std::string_view name) {
    return std::format("_br7${}.{}.{}", bry_mod.id, bry_mod.name, name);
}

std::string CodeGenerator::mangleName(Module& imported_bry_mod, std::string_view name) {
    return std::format("_br7${}.{}.{}", imported_bry_mod.id, imported_bry_mod.name, name);
}
#include "codegen.hpp"

#include <iostream>

#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"

void CodeGenerator::genDeclProto(Decl* decl) {
    auto* node = decl->hir_decl;

    switch (node->kind) {
    case HIR_FUNC:
        genFuncProto(decl);
        break;
    case HIR_GLOBAL_VAR:
        genGlobalVarDecl(decl);
        break;
    case HIR_GLOBAL_CONST:
        genGlobalConst(decl);
        break;
    case HIR_STRUCT:
        // TODO: handle struct metadata
        genType(node->ir_TypeDef.symbol->type, true);
        break;
    case HIR_ALIAS:
        // TODO: handle alias metadata
        genType(node->ir_TypeDef.symbol->type, true);
        break;
    default:
        Panic("top declaration codegen not implemented for {}", (int)node->kind);
    }
}

void CodeGenerator::genDeclBody(Decl* decl) {
    auto* node = decl->hir_decl;

    switch (node->kind) {
    case HIR_FUNC:
        genFuncBody(decl);
        break;
    case HIR_GLOBAL_VAR:
        genGlobalVarInit(node);
        break;
    case HIR_GLOBAL_CONST: case HIR_STRUCT: case HIR_ALIAS: case HIR_ENUM:
        // Nothing to do :)
        break;
    default:
        Panic("predicate codegen not implemented for {}", (int)node->kind);
    }
}

/* -------------------------------------------------------------------------- */

void CodeGenerator::genFuncProto(Decl* decl) {
    auto* node = decl->hir_decl;

    auto* symbol = node->ir_Func.symbol;

    auto* ll_type = genType(symbol->type);
    Assert(ll_type->isFunctionTy(), "function signature is not a function type in codegen");

    auto* ll_func_type = llvm::dyn_cast<llvm::FunctionType>(ll_type);

    std::string ll_name {};
    bool exported = symbol->flags & SYM_EXPORTED;
    llvm::CallingConv::ID cconv = llvm::CallingConv::C;
    bool inline_hint = false;
    for (auto& attr : decl->attrs) {
        if (attr.name == "extern" || attr.name == "abientry") {
            exported = true;
            ll_name = attr.value.size() == 0 ? symbol->name : attr.value;
        } else if (attr.name == "callconv") {
            cconv = cconv_name_to_id[attr.value];
        } else if (attr.name == "inline") {
            inline_hint = true;
        }
    }

    if (ll_name.size() == 0) {
        ll_name = mangleName(symbol->name);
    }

    llvm::Function* ll_func = llvm::Function::Create(
        ll_func_type, 
        exported ? llvm::Function::ExternalLinkage : llvm::Function::PrivateLinkage,
        ll_name,
        mod
    );

    ll_func->setCallingConv(cconv);

    if (inline_hint) {
        ll_func->addFnAttr(llvm::Attribute::InlineHint);
    }

    size_t offset = ll_func->arg_size() > node->ir_Func.params.size();
    for (size_t i = 0; i < node->ir_Func.params.size(); i++) {
        auto arg = ll_func->getArg(i + offset);

        arg->setName(node->ir_Func.params[i]->name);
        node->ir_Func.params[i]->llvm_value = arg;
    }

    symbol->llvm_value = ll_func;
}

void CodeGenerator::genFuncBody(Decl* decl) {
    auto* node = decl->hir_decl;

    debug.BeginFuncBody(decl, llvm::dyn_cast<llvm::Function>(node->ir_Func.symbol->llvm_value));
    debug.ClearDebugLocation();

    Assert(llvm::Function::classof(node->ir_Func.symbol->llvm_value), "function def is not a function value in codegen");
    auto* ll_func = llvm::dyn_cast<llvm::Function>(node->ir_Func.symbol->llvm_value);

    var_block = llvm::BasicBlock::Create(ctx, "entry", ll_func);
    setCurrentBlock(var_block);

    for (auto* param : node->ir_Func.params) {
        auto* ll_type = genType(param->type, true);
        auto* ll_param = irb.CreateAlloca(ll_type);

        if (shouldPtrWrap(ll_type)) {
            genStructCopy(ll_type, param->llvm_value, ll_param);
        } else {
            irb.CreateStore(param->llvm_value, ll_param);
        }
        
        param->llvm_value = ll_param;
    }

    if (ll_func->arg_size() > node->ir_Func.params.size()) {
        return_param = &(*ll_func->arg_begin());
    } else {
        return_param = nullptr;
    }

    ll_enclosing_func = ll_func;

    auto* body_block = appendBlock();
    setCurrentBlock(body_block);
    
    genStmt(node->ir_Func.body);
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

void CodeGenerator::genGlobalVarDecl(Decl* decl) {
    auto& aglobal = decl->hir_decl->ir_GlobalVar;
    auto* symbol = aglobal.symbol;

    debug.SetCurrentFile(src_mod.files[decl->file_number]);
    
    auto* ll_type = genType(symbol->type, true);

    // TODO: handle attributes
    Assert(decl->attrs.size() == 0, "attributes for global variables not implemented");

    llvm::Constant* init_value;
    if (aglobal.const_init == nullptr) {
        init_value = llvm::Constant::getNullValue(ll_type);
    } else {
        auto comptime_flags = symbol->flags & SYM_EXPORTED ? CTG_EXPORTED : CTG_NONE;
        init_value = genComptime(aglobal.const_init, comptime_flags | CTG_UNWRAPPED, symbol->type);
    }
    
    bool exported = symbol->flags & SYM_EXPORTED;
    auto gv = new llvm::GlobalVariable(
        mod, 
        ll_type, 
        symbol->immut, 
        exported ? llvm::GlobalValue::ExternalLinkage : llvm::GlobalValue::PrivateLinkage, 
        init_value, 
        mangleName(symbol->name)
    );
    debug.EmitGlobalVariableInfo(decl, gv);

    if (symbol->flags & SYM_CONST) {
        symbol->flags ^= SYM_VAR | SYM_CONST;
    }

    symbol->llvm_value = gv;
}

void CodeGenerator::genGlobalVarInit(HirDecl* node) {
    if (!node->ir_GlobalVar.init || node->ir_GlobalVar.const_init) {
        return;
    }

    // No debug info for global variable expressions.
    debug.PushDisable();

    setCurrentBlock(ll_init_block);

    ll_enclosing_func = ll_init_func;
    genStoreExpr(node->ir_GlobalVar.init, node->ir_GlobalVar.symbol->llvm_value);
    ll_enclosing_func = nullptr;
    
    ll_init_block = getCurrentBlock();

    debug.PopDisable();
}

/* -------------------------------------------------------------------------- */

std::string CodeGenerator::mangleName(std::string_view name) {
    return std::format("_br7${}.{}.{}", src_mod.id, src_mod.name, name);
}

std::string CodeGenerator::mangleName(Module& imported_bry_mod, std::string_view name) {
    return std::format("_br7${}.{}.{}", imported_bry_mod.id, imported_bry_mod.name, name);
}
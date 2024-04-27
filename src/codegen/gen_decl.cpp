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
    case HIR_METHOD:
        genMethodProto(decl);
        break;
    case HIR_FACTORY:
        genFactoryProto(decl);
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
    case HIR_ENUM:
        // Nothing to do :)
        break;
    default:
        Panic("declaration prototype codegen not implemented for {}", (int)node->kind);
    }
}

void CodeGenerator::genDeclBody(Decl* decl) {
    auto* node = decl->hir_decl;

    switch (node->kind) {
    case HIR_FUNC:
        genFuncBody(decl);
        break;
    case HIR_METHOD:
        genMethodBody(decl);
        break;
    case HIR_FACTORY:
        genFactoryBody(decl);
        break;
    case HIR_GLOBAL_VAR:
        genGlobalVarInit(node);
        break;
    case HIR_GLOBAL_CONST: case HIR_STRUCT: case HIR_ALIAS: case HIR_ENUM:
        // Nothing to do :)
        break;
    default:
        Panic("declaration body codegen not implemented for {}", (int)node->kind);
    }
}

/* -------------------------------------------------------------------------- */

void CodeGenerator::genFuncProto(Decl* decl) {
    auto* node = decl->hir_decl;
    auto* symbol = node->ir_Func.symbol;

    auto* ll_func_type = genFuncType(symbol->type);

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

    auto* ll_func = llvm::Function::Create(
        ll_func_type, 
        exported ? llvm::Function::ExternalLinkage : llvm::Function::PrivateLinkage,
        ll_name,
        mod
    );

    ll_func->setCallingConv(cconv);

    if (inline_hint) {
        ll_func->addFnAttr(llvm::Attribute::InlineHint);
    }

    size_t offset = shouldPtrWrap(node->ir_Func.return_type) ? 1 : 0;
    for (size_t i = 0; i < node->ir_Func.params.size(); i++) {
        auto arg = ll_func->getArg(i + offset);

        arg->setName(node->ir_Func.params[i]->name);
        node->ir_Func.params[i]->llvm_value = arg;
    }

    symbol->llvm_value = ll_func;
}

void CodeGenerator::genMethodProto(Decl* decl) {
    auto* node = decl->hir_decl;
    auto* method = node->ir_Method.method;

    auto* ll_func_type = genFuncType(method->signature, true);

    bool inline_hint = false;
    for (auto& attr : decl->attrs) {
        if (attr.name == "inline") {
            inline_hint = true;
            break;
        }
    }

    auto ll_name = std::format("{}.{}", node->ir_Method.bind_type->ToString(), method->name);
    ll_name = mangleName(ll_name);

    auto* ll_func = llvm::Function::Create(
        ll_func_type, 
        method->exported ? llvm::Function::ExternalLinkage : llvm::Function::PrivateLinkage,
        ll_name,
        mod
    );

    if (inline_hint) {
        ll_func->addFnAttr(llvm::Attribute::InlineHint);
    }

    size_t offset = shouldPtrWrap(node->ir_Method.return_type) ? 2 : 1;
    for (size_t i = 0; i < node->ir_Method.params.size(); i++) {
        auto arg = ll_func->getArg(i + offset);

        arg->setName(node->ir_Method.params[i]->name);
        node->ir_Method.params[i]->llvm_value = arg;
    }

    auto arg = ll_func->getArg(offset - 1);
    arg->setName("self");
    node->ir_Method.self_ptr->llvm_value = arg;

    method->llvm_value = ll_func;
}

void CodeGenerator::genFactoryProto(Decl* decl) {
    auto* node = decl->hir_decl;
    auto* factory = node->ir_Factory.func;

    auto* ll_func_type = genFuncType(factory->signature);

    bool inline_hint = false;
    for (auto& attr : decl->attrs) {
        if (attr.name == "inline") {
            inline_hint = true;
            break;
        }
    }

    auto ll_name = std::format("{}._$ftry", node->ir_Factory.bind_type->ToString());
    ll_name = mangleName(ll_name);

    auto* ll_func = llvm::Function::Create(
        ll_func_type, 
        factory->exported ? llvm::Function::ExternalLinkage : llvm::Function::PrivateLinkage,
        ll_name,
        mod
    );

    if (inline_hint) {
        ll_func->addFnAttr(llvm::Attribute::InlineHint);
    }

    size_t offset = shouldPtrWrap(node->ir_Factory.return_type) ? 1 : 0;
    for (size_t i = 0; i < node->ir_Factory.params.size(); i++) {
        auto arg = ll_func->getArg(i + offset);

        arg->setName(node->ir_Factory.params[i]->name);
        node->ir_Factory.params[i]->llvm_value = arg;
    }

    factory->llvm_value = ll_func;
}

llvm::FunctionType* CodeGenerator::genFuncType(Type* type, bool has_self_ptr) {
    type = type->Inner();
    Assert(type->kind == TYPE_FUNC, "expected a function type in codegen but got {}", type->ToString());
    auto& func_type = type->ty_Func;

    std::vector<llvm::Type*> ll_param_types;
    llvm::Type* ll_return_type;
    if (shouldPtrWrap(func_type.return_type)) {
        ll_param_types.push_back(genType(func_type.return_type));
        ll_return_type = llvm::Type::getVoidTy(ctx);
    } else {
        ll_return_type = genType(func_type.return_type);
    }

    if (has_self_ptr) {
        ll_param_types.push_back(llvm::PointerType::get(ctx, 0));
    }

    for (auto* param_type : func_type.param_types) {
        ll_param_types.push_back(genType(param_type));
    }

    return llvm::FunctionType::get(ll_return_type, ll_param_types, false);
}

/* -------------------------------------------------------------------------- */

void CodeGenerator::genFuncBody(Decl* decl) {
    auto* node = decl->hir_decl;
    if (node->ir_Func.body == nullptr) {
        return;
    }

    debug.BeginFuncBody(decl, llvm::dyn_cast<llvm::Function>(node->ir_Func.symbol->llvm_value));
    debug.ClearDebugLocation();

    auto* ll_func = llvm::dyn_cast<llvm::Function>(node->ir_Func.symbol->llvm_value);
    var_block = llvm::BasicBlock::Create(ctx, "entry", ll_func);

    genInnerFuncBody(ll_func, node->ir_Func.params, node->ir_Func.body);

    debug.EndFuncBody();
}

void CodeGenerator::genMethodBody(Decl* decl) {
    auto* node = decl->hir_decl;

    // TODO: method debug info
    debug.ClearDebugLocation();

    auto* ll_func = llvm::dyn_cast<llvm::Function>(node->ir_Method.method->llvm_value);
    var_block = llvm::BasicBlock::Create(ctx, "entry", ll_func);
    setCurrentBlock(var_block);

    auto* ll_self_ptr = irb.CreateAlloca(llvm::PointerType::get(ctx, 0));
    irb.CreateStore(node->ir_Method.self_ptr->llvm_value, ll_self_ptr);
    node->ir_Method.self_ptr->llvm_value = ll_self_ptr;
    
    genInnerFuncBody(ll_func, node->ir_Method.params, node->ir_Method.body);

    // TODO: end method debug info
}

void CodeGenerator::genFactoryBody(Decl* decl) {
    auto* node = decl->hir_decl;

    // TODO: factory debug info
    debug.ClearDebugLocation();

    auto* ll_func = llvm::dyn_cast<llvm::Function>(node->ir_Factory.func->llvm_value);
    var_block = llvm::BasicBlock::Create(ctx, "entry", ll_func);
    
    genInnerFuncBody(ll_func, node->ir_Factory.params, node->ir_Factory.body);

    // TODO: end factory debug info
}

void CodeGenerator::genInnerFuncBody(llvm::Function* ll_func, std::span<Symbol*> params, HirStmt* body) {
    setCurrentBlock(var_block);

    for (auto* param : params) {
        auto* ll_type = genType(param->type, true);
        auto* ll_param = irb.CreateAlloca(ll_type);

        if (shouldPtrWrap(ll_type)) {
            genMemCopy(ll_type, param->llvm_value, ll_param);
        } else {
            irb.CreateStore(param->llvm_value, ll_param);
        }
        
        param->llvm_value = ll_param;
    }

    if (ll_func->arg_size() > params.size()) {
        return_param = &(*ll_func->arg_begin());
    } else {
        return_param = nullptr;
    }

    ll_enclosing_func = ll_func;

    auto* body_block = appendBlock();
    setCurrentBlock(body_block);
    
    genStmt(body);
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

void CodeGenerator::genGlobalConst(Decl* decl) {
    auto& hconst = decl->hir_decl->ir_GlobalConst;

    ComptimeGenFlags comptime_flags = CTG_CONST;
    if (hconst.symbol->flags & SYM_EXPORTED) {
        comptime_flags |= CTG_EXPORTED;
    }

    hconst.symbol->llvm_value = genComptime(hconst.init, comptime_flags, hconst.symbol->type);
}

/* -------------------------------------------------------------------------- */

std::string CodeGenerator::mangleName(std::string_view name) {
    return std::format("_br7${}.{}.{}", src_mod.id, src_mod.name, name);
}

std::string CodeGenerator::mangleName(Module& imported_bry_mod, std::string_view name) {
    return std::format("_br7${}.{}.{}", imported_bry_mod.id, imported_bry_mod.name, name);
}
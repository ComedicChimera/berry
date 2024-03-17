#include "codegen.hpp"

MainBuilder::MainBuilder(llvm::LLVMContext& ctx, llvm::Module& main_mod)
: ctx(ctx)
, main_mod(main_mod)
, irb(ctx)
{
    // Add the `_fltused` global symbol (enable floating point).
    auto* ll_double_type = llvm::Type::getDoubleTy(ctx);
    new llvm::GlobalVariable(
        main_mod,
        ll_double_type,
        true,
        llvm::GlobalValue::ExternalLinkage,
        llvm::Constant::getNullValue(ll_double_type),
        "_fltused"
    );

    // Create the main function.
    rt_stub_func_type = llvm::FunctionType::get(llvm::Type::getVoidTy(main_mod.getContext()), false);
    rt_main_func = llvm::Function::Create(
        rt_stub_func_type,
        llvm::Function::ExternalLinkage,
        "__berry_main",
        main_mod
    );

    auto* rt_main_block = llvm::BasicBlock::Create(ctx, "", rt_main_func);
    irb.SetInsertPoint(rt_main_block);
}

void MainBuilder::GenInitCall(llvm::Function* ll_init_func) {
    // Add init call to the main module.
    auto* ll_init_func_stub = llvm::Function::Create(
        dyn_cast<llvm::Function>(ll_init_func)->getFunctionType(),
        llvm::Function::ExternalLinkage,
        ll_init_func->getName(),
        main_mod
    );

    irb.CreateCall(ll_init_func_stub);
}

void MainBuilder::GenUserMainCall(Module& root_mod) {        
    // Check that a valid main function exists.
    auto it = root_mod.symbol_table.find("main");
    if (it == root_mod.symbol_table.end()) {
        ReportFatal("input module does not have a main function");
    }

    auto* sym = it->second;
    if ((sym->flags & SYM_FUNC) == 0 || sym->type->kind != TYPE_FUNC) {
        ReportFatal("input module does not have a main function");
    }

    if (sym->type->ty_Func.param_types.size() != 0 || sym->type->ty_Func.return_type->kind != TYPE_UNIT) {
        auto* def = root_mod.defs[sym->def_number];
        auto& src_file = root_mod.files[def->parent_file_number];
        
        ReportCompileError(src_file.display_path, sym->span, "main function must take no arguments and return no value");
    }

    // Make sure the main function is externally visible (so we can call it).
    Assert(llvm::Function::classof(sym->llvm_value), "main function is not an llvm::Function");
    auto* foreign_main_func = llvm::dyn_cast<llvm::Function>(sym->llvm_value);
    foreign_main_func->setLinkage(llvm::Function::ExternalLinkage);

    // Insert the call to the main function.
    auto* user_main_func = llvm::Function::Create(
        rt_stub_func_type, 
        llvm::Function::ExternalLinkage, 
        foreign_main_func->getName(), 
        main_mod
    );
    
    irb.CreateCall(user_main_func);
}

void MainBuilder::FinishMain() {
    irb.CreateRetVoid();
}

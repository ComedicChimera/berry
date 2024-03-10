#include "loader.hpp"

struct InitGenerator {
    llvm::FunctionType* rt_stub_func_type { nullptr };
    llvm::Module& main_mod;
    llvm::IRBuilder<>& irb;
    std::vector<bool>& visited;

    void GenInitCalls(Module& src_mod) {
        // Make sure we don't visit modules multiple times.
        if (visited[src_mod.id])
            return;

        visited[src_mod.id] = true;

        // Generate the init calls for all the child modules of this one first.
        for (auto& dep : src_mod.deps) {
            GenInitCalls(*dep.mod);
        }

        // Create the module's init function.
        auto* ll_init_func = llvm::Function::Create(
            rt_stub_func_type,
            llvm::Function::ExternalLinkage,
            std::format("__berry_init_mod${}", src_mod.id),
            main_mod
        );

        // Call the init function directly.
        irb.CreateCall(ll_init_func);
    }
};

void Loader::GenerateMainModule(llvm::Module& main_mod, bool needs_user_main) {
    // Add _fltused global symbol.
    auto* ll_double_type = llvm::Type::getDoubleTy(main_mod.getContext());
    auto* gv_fltused = new llvm::GlobalVariable(
        main_mod,
        ll_double_type,
        true,
        llvm::GlobalValue::ExternalLinkage,
        llvm::Constant::getNullValue(ll_double_type),
        "_fltused"
    );

    // Create the main function.
    auto* rt_stub_func_type = llvm::FunctionType::get(llvm::Type::getVoidTy(main_mod.getContext()), false);
    auto main_func = llvm::Function::Create(
        rt_stub_func_type,
        llvm::Function::ExternalLinkage,
        "__berry_main",
        main_mod
    );

    // Build the main function block.
    auto main_block = llvm::BasicBlock::Create(main_mod.getContext(), "", main_func);
    llvm::IRBuilder<> irb(main_mod.getContext());
    irb.SetInsertPoint(main_block);

    // Generate all the initializer calls.
    std::vector<bool> visited(mod_table.size(), false);
    InitGenerator ig { rt_stub_func_type, main_mod, irb, visited };
    ig.GenInitCalls(*root_mod);

    // Call the user main if necessary.
    if (needs_user_main) {
        // Check that a valid main function exists.
        auto it = root_mod->symbol_table.find("main");
        if (it == root_mod->symbol_table.end()) {
            ReportFatal("input module does not have a main function");
        }

        auto* sym = it->second;
        if ((sym->flags & SYM_FUNC) || sym->type->kind != TYPE_FUNC) {
            ReportFatal("input module does not have a main function");
        }

        if (sym->type->ty_Func.param_types.size() != 0 || sym->type->ty_Func.return_type->kind != TYPE_UNIT) {
            // TODO: can we easily get the offending source file here?
            ReportFatal("main function must take no arguments and return no value");
        }

        // Make sure the main function is externally visible (so we can call it).
        Assert(llvm::Function::classof(sym->llvm_value), "main function is not an llvm::Function");
        auto* ll_foreign_main_func = llvm::dyn_cast<llvm::Function>(sym->llvm_value);
        ll_foreign_main_func->setLinkage(llvm::Function::ExternalLinkage);

        // Insert the call to the main function.
        auto* ll_main_func = llvm::Function::Create(
            rt_stub_func_type, 
            llvm::Function::ExternalLinkage, 
            ll_foreign_main_func->getName(), 
            main_mod
        );
        
        irb.CreateCall(ll_main_func);
    }

    // End __berry_main
    irb.CreateRetVoid();
}

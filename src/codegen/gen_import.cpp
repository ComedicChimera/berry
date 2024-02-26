#include "codegen.hpp"

void CodeGenerator::genImports() {
    size_t i = 0;
    for (auto& dep : bry_mod.deps) {
        for (auto export_num : dep.usages) {
            auto& entry = dep.mod->export_table[export_num];

            switch (entry.def->kind) {
            case AST_FUNC:
                loaded_imports[i].emplace(export_num, genImportFunc(*dep.mod, entry.def));
                break;
            case AST_GLOBAL_VAR:
                loaded_imports[i].emplace(export_num, genImportGlobalVar(*dep.mod, entry.def));
                break;
            }
        }

        i++;
    }
}

/* -------------------------------------------------------------------------- */

llvm::Value* CodeGenerator::genImportFunc(Module& imported_mod, AstDef* node) {
    auto* symbol = node->an_Func.symbol;

    auto* ll_type = genType(symbol->type);
    Assert(ll_type->isFunctionTy(), "function signature is not a function type in codegen");

    auto* ll_func_type = llvm::dyn_cast<llvm::FunctionType>(ll_type);

    bool should_mangle = true;
    llvm::CallingConv::ID cconv = llvm::CallingConv::C;
    for (auto& tag : node->metadata) {
        if (tag.name == "extern" || tag.name == "abientry") {
            should_mangle = false;
        } else if (tag.name == "callconv") {
            cconv = cconv_name_to_id[tag.value];
        }
    }

    std::string ll_name { should_mangle ? mangleName(imported_mod, symbol->name) : symbol->name };

    llvm::Function* ll_func = llvm::Function::Create(
        ll_func_type, 
        llvm::Function::ExternalLinkage,
        ll_name,
        mod
    );

    ll_func->setCallingConv(cconv);
    return ll_func;
}

llvm::Value* CodeGenerator::genImportGlobalVar(Module& imported_mod, AstDef* node) {
    auto* symbol = node->an_GlobalVar.symbol;

    auto* ll_type = genType(symbol->type);

    // TODO: handle metadata
    Assert(node->metadata.size() == 0, "metadata for global variables not implemented");

    return new llvm::GlobalVariable(
        mod, 
        ll_type, 
        false, 
        llvm::GlobalValue::ExternalLinkage, 
        nullptr, 
        mangleName(imported_mod, symbol->name)
    );
}
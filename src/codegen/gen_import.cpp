#include "codegen.hpp"

void CodeGenerator::genImports() {
    size_t i = 0;
    for (auto& dep : src_mod.deps) {
        for (auto decl_num : dep.usages) {
            auto* decl = dep.mod->sorted_decls[decl_num];
            auto* hir_decl = decl->hir_decl;

            switch (hir_decl->kind) {
            case HIR_FUNC:
                loaded_imports[i].emplace(decl_num, genImportFunc(*dep.mod, decl));
                break;
            case HIR_METHOD:
                loaded_imports[i].emplace(decl_num, genImportMethod(*dep.mod, decl));
                break;
            case HIR_FACTORY:
                loaded_imports[i].emplace(decl_num, genImportFactory(*dep.mod, decl));
                break;
            case HIR_GLOBAL_VAR:
                loaded_imports[i].emplace(decl_num, genImportGlobalVar(*dep.mod, decl));
                break;
            case HIR_GLOBAL_CONST: {
                auto* ll_const = genComptime(
                    hir_decl->ir_GlobalConst.init, 
                    CTG_EXPORTED, 
                    hir_decl->ir_GlobalConst.symbol->type
                );

                loaded_imports[i].emplace(decl_num, ll_const);
            } break;
            case HIR_STRUCT:
                // TODO: handle struct metadata
                genType(hir_decl->ir_TypeDef.symbol->type);
                break;
            }
        }

        i++;
    }
}

/* -------------------------------------------------------------------------- */

llvm::Value* CodeGenerator::genImportFunc(Module& imported_mod, Decl* decl) {
    auto* node = decl->hir_decl;
    auto* symbol = node->ir_Func.symbol;

    auto* ll_func_type = genFuncType(symbol->type);

    std::string ll_name {};
    llvm::CallingConv::ID cconv = llvm::CallingConv::C;
    for (auto& attr : decl->attrs) {
        if (attr.name == "extern" || attr.name == "abientry") {
            ll_name = attr.value.size() == 0 ? symbol->name : attr.value;
        } else if (attr.name == "callconv") {
            cconv = cconv_name_to_id[attr.value];
        }
    }

    if (ll_name.size() == 0) {
        ll_name = mangleName(imported_mod, symbol->name);
    }

    auto* ll_func = llvm::Function::Create(
        ll_func_type, 
        llvm::Function::ExternalLinkage,
        ll_name,
        mod
    );

    ll_func->setCallingConv(cconv);
    return ll_func;
}

llvm::Value* CodeGenerator::genImportMethod(Module& imported_mod, Decl* decl) {
    auto* node = decl->hir_decl;
    auto* method = node->ir_Method.method;

    auto* ll_func_type = genFuncType(method->signature, true);

    auto ll_name = std::format("{}.{}", node->ir_Method.bind_type->ty_Named.name, method->name);
    ll_name = mangleName(imported_mod, ll_name);

    auto* ll_func = llvm::Function::Create(
        ll_func_type, 
        llvm::Function::ExternalLinkage,
        ll_name,
        mod
    );

    return ll_func;
}

llvm::Value* CodeGenerator::genImportFactory(Module& imported_mod, Decl* decl) {
    auto* node = decl->hir_decl;
    auto* factory = node->ir_Factory.func;

    auto* ll_func_type = genFuncType(factory->signature);

    auto ll_name = std::format("{}._$ftry", node->ir_Factory.bind_type->ty_Named.name);
    ll_name = mangleName(imported_mod, ll_name);

    auto* ll_func = llvm::Function::Create(
        ll_func_type, 
        llvm::Function::ExternalLinkage,
        ll_name,
        mod
    );

    return ll_func;
}

llvm::Value* CodeGenerator::genImportGlobalVar(Module& imported_mod, Decl* decl) {
    auto* node = decl->hir_decl;
    auto* symbol = node->ir_GlobalVar.symbol;


    auto* ll_type = genType(symbol->type);

    // TODO: handle attributes
    Assert(decl->attrs.size() == 0, "attributes for global variables not implemented");

    return new llvm::GlobalVariable(
        mod, 
        ll_type, 
        false, 
        llvm::GlobalValue::ExternalLinkage, 
        nullptr, 
        mangleName(imported_mod, symbol->name)
    );
}
#include <iostream>
#include <fstream>

#include "llvm/IR/Verifier.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/IR/LegacyPassManager.h"

#include "parser.hpp"
#include "checker.hpp"
#include "codegen.hpp"
#include "linker.hpp"

#include "test/ast_print.hpp"

bool compileFile(Module& mod, SourceFile& src_file, std::ifstream& file) {  
    Arena arena;
    Parser p(arena, file, src_file);
    p.ParseFile();

    if (ErrorCount() > 0) {
        return false;
    }

    Checker c(arena, src_file); 
    for (auto* def : src_file.defs) {
        c.CheckDef(def);
    }

    if (ErrorCount() > 0) {
        return false;
    }

    // PrintAst(src_file);
    // return true;

    auto native_target_triple = llvm::sys::getDefaultTargetTriple();

    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmParser();
    llvm::InitializeNativeTargetAsmPrinter();

    std::string err_msg;
    auto* target = llvm::TargetRegistry::lookupTarget(native_target_triple, err_msg);
    if (!target) {
        std::cout << "error: finding native target: " << err_msg << '\n';
        return false;
    }

    llvm::TargetOptions target_opt;
    auto* target_machine = target->createTargetMachine(native_target_triple, "generic", "", target_opt, llvm::Reloc::PIC_);

    llvm::LLVMContext ll_ctx;
    llvm::Module ll_mod(mod.name, ll_ctx);
    ll_mod.setDataLayout(target_machine->createDataLayout());
    ll_mod.setTargetTriple(native_target_triple);

    CodeGenerator cg(ll_ctx, ll_mod, mod);
    cg.GenerateModule();

    std::error_code err_code;

    // // DEBUG: Print module.
    // llvm::raw_fd_ostream p_out_file("out.ll", err_code);
    // if (err_code) {
    //     std::cerr << "error: creating output fd stream for module printing: " << err_code.message() << '\n';
    //     return false;
    // }
    // ll_mod.print(p_out_file, nullptr);
    // p_out_file.close();
    // return true;

    llvm::raw_fd_ostream out_file("out.o", err_code, llvm::sys::fs::OF_None);
    if (err_code) {
        std::cout << "error: opening output file: " << err_code.message() << '\n';
        return false;
    }

    llvm::legacy::PassManager pass;
    if (target_machine->addPassesToEmitFile(pass, out_file, nullptr, llvm::CodeGenFileType::CGFT_ObjectFile)) {
        std::cout << "error: target machine was unable to generate output file\n";
        return false;
    }
    pass.run(ll_mod);
    out_file.flush();
    out_file.close();

    LinkConfig config { "out.exe" };
    config.obj_files.emplace_back("out.o");
    bool link_result = RunLinker(config);
    RemoveObjFile("out.o");

    return link_result;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "error: usage: berry <filename>\n");
        return 1;
    }

    char* file_name = argv[1];

    std::ifstream file;
    file.open(file_name);
    if (!file) {
        fprintf(stderr, "error: failed to open file: %s\n", strerror(errno));
        return 1;
    }

    Module mod { 0, "main" };
    mod.files.emplace_back(0, &mod, file_name, file_name);
    auto& src_file = mod.files[0];

    bool ok = compileFile(mod, src_file, file);

    file.close();

    return ok ? 0 : 1;
}

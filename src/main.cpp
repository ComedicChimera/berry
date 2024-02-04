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

bool compileFile(Module& mod, SourceFile& src_file, std::ifstream& file) {  
    Arena arena;
    Parser p(arena, file, src_file);
    p.ParseFile();

    if (ErrorCount() > 0) {
        return false;
    }

    Checker c(arena, src_file); 
    for (auto& def : src_file.defs) {
        def->Accept(&c);
    }

    if (ErrorCount() > 0) {
        return false;
    }

    llvm::LLVMContext ll_ctx;
    llvm::Module ll_mod(mod.name, ll_ctx);
    CodeGenerator cg(ll_ctx, ll_mod, mod);
    cg.GenerateModule();

    if (llvm::verifyModule(ll_mod, &llvm::outs())) {
        std::cout << "\n\nprinting module:\n";
        ll_mod.print(llvm::outs(), nullptr);
        return false;
    }

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

    ll_mod.setDataLayout(target_machine->createDataLayout());
    ll_mod.setTargetTriple(native_target_triple);

    std::error_code err_code;
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

    return true;
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

// //===- examples/ModuleMaker/ModuleMaker.cpp - Example project ---*- C++ -*-===//
// //
// // Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// // See https://llvm.org/LICENSE.txt for license information.
// // SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// //
// //===----------------------------------------------------------------------===//
// //
// // This programs is a simple example that creates an LLVM module "from scratch",
// // emitting it as a bitcode file to standard out.  This is just to show how
// // LLVM projects work and to demonstrate some of the LLVM APIs.
// //
// //===----------------------------------------------------------------------===//

// #include "llvm/IR/BasicBlock.h"
// #include "llvm/IR/Constants.h"
// #include "llvm/IR/DerivedTypes.h"
// #include "llvm/IR/Function.h"
// #include "llvm/IR/InstrTypes.h"
// #include "llvm/IR/Instruction.h"
// #include "llvm/IR/Instructions.h"
// #include "llvm/IR/LLVMContext.h"
// #include "llvm/IR/Module.h"
// #include "llvm/IR/Type.h"
// #include "llvm/Support/raw_ostream.h"

// using namespace llvm;

// int main() {
//     LLVMContext Context;

//     // Create the "module" or "program" or "translation unit" to hold the
//     // function
//     Module *M = new Module("test", Context);

//     // Create the main function: first create the type 'int ()'
//     FunctionType *FT =
//     FunctionType::get(Type::getInt32Ty(Context), /*not vararg*/false);

//     // By passing a module as the last parameter to the Function constructor,
//     // it automatically gets appended to the Module.
//     Function *F = Function::Create(FT, Function::ExternalLinkage, "main", M);

//     // Add a basic block to the function... again, it automatically inserts
//     // because of the last argument.
//     BasicBlock *BB = BasicBlock::Create(Context, "EntryBlock", F);

//     // Get pointers to the constant integers...
//     Value *Two = ConstantInt::get(Type::getInt32Ty(Context), 2);
//     Value *Three = ConstantInt::get(Type::getInt32Ty(Context), 3);

//     // Create the add instruction... does not insert...
//     Instruction *Add = BinaryOperator::Create(Instruction::Add, Two, Three,
//                                             "addresult");

//     // explicitly insert it into the basic block...
//     Add->insertInto(BB, BB->end());

//     // Create the return instruction and add it to the basic block
//     ReturnInst::Create(Context, Add)->insertInto(BB, BB->end());

//     M->dump();

//     // Delete the module and all of its contents.
//     delete M;
//     return 0;
// }

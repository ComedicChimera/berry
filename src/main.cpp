#include <iostream>
#include <fstream>

#include "parser.hpp"
#include "checker.hpp"
#include "test/ast_print.hpp"

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
    SourceFile src_file { 0, &mod, file_name, file_name };
    
    Arena arena;
    Parser p(arena, file, src_file);
    p.ParseFile();

    AstPrinter printer;
    if (ErrorCount() == 0) {
        Checker c(arena, src_file);
        
        for (auto& def : src_file.defs) {
            def->Check(c);

            def->Accept(&printer);
            std::cout << "\n\n";
        }

        std::cout.flush();
    }

    file.close();
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

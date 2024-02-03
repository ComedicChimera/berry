#ifndef CODEGEN_H_INC
#define CODEGEN_H_INC

#include "llvm/IR/Module.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/IRBuilder.h"

#include "visitor.hpp"

// CodeGenerator compiles a Berry module to an LLVM module.
class CodeGenerator : public Visitor {
    // ctx is the LLVM context being compiled in.
    llvm::LLVMContext& ctx;

    // mod is the LLVM module being generated.
    llvm::Module& mod;

    // builder is the IR builder being used.
    llvm::IRBuilder<> builder;
    
    // bry_mod is the Berry module being compiled.
    Module& bry_mod;

    /* ---------------------------------------------------------------------- */

    // var_block is the block to append variable allocas to.
    llvm::BasicBlock* var_block;

    // value_mode_stack keeps track of whether the generator should generate
    // expressions to produce values or addresses.  For example, the expression
    // `a` usually compiled with a load instruction, but if we are referencing
    // it (ex: `&a`), then we just want to return the address of `a`.
    std::vector<bool> value_mode_stack;

    // pred_mode indicates whether we are visiting in predicate mode (ie.
    // generating executable code) or in declare mode (ie. only creating
    // definitions).
    bool pred_mode;

public:
    // Creates a new code generator using ctx and outputting to mod.
    CodeGenerator(llvm::LLVMContext& ctx, llvm::Module& mod, Module& bry_mod)
    : ctx(ctx), mod(mod), builder(ctx)
    , bry_mod(bry_mod)
    , var_block(nullptr)
    , pred_mode(false)
    {}

    // GenerateModule compiles the module.
    void GenerateModule();

    // Visitor methods
    void Visit(AstFuncDef& node) override;
    void Visit(AstLocalVarDef &node) override;
    void Visit(AstGlobalVarDef& node) override;
    void Visit(AstBlock& node) override;
    void Visit(AstCast& node) override;
    void Visit(AstBinaryOp& node) override;
    void Visit(AstUnaryOp& node) override;
    void Visit(AstAddrOf& node) override;
    void Visit(AstDeref& node) override;
    void Visit(AstCall& node) override;
    void Visit(AstIdent& node) override;
    void Visit(AstIntLit& node) override;
    void Visit(AstFloatLit& node) override;
    void Visit(AstBoolLit& node) override;
    void Visit(AstNullLit& node) override;

private:
    // genFuncBody generates the function body for node.
    void genFuncBody(AstFuncDef &node);

    // genType converts the Berry type type to an LLVM type.
    llvm::Type *genType(Type *type);

    // mangleName converts a Berry global symbol name into its ABI equivalent.
    std::string mangleName(std::string_view name);

    /* ---------------------------------------------------------------------- */

    bool isValueMode();
    void pushValueMode(bool mode);
    void popValueMode();
};

#endif
#include "codegen.hpp"

#include <iostream>

#include "llvm/IR/Verifier.h"

void CodeGenerator::GenerateModule() {
    createBuiltinGlobals();

    for (auto& file : bry_mod.files) {
        debug.EmitFileInfo(file);
        debug.SetCurrentFile(file);

        for (auto* def : file.defs) {
            genTopDecl(def);
        }
    }

    getBuiltinFuncs();

    for (auto& file : bry_mod.files) {
        debug.SetCurrentFile(file);

        for (auto* def : file.defs) {
            genPredicates(def);
        }
    }

    finishModule();    
}

/* -------------------------------------------------------------------------- */

void CodeGenerator::createBuiltinGlobals() {
    // Add _fltused global symbol.
    auto* ll_double_type = llvm::Type::getDoubleTy(ctx);
    auto* gv_fltused = new llvm::GlobalVariable(
        mod,
        ll_double_type,
        true,
        llvm::GlobalValue::ExternalLinkage,
        llvm::Constant::getNullValue(ll_double_type),
        "_fltused"
    );

    // Declare the global array type.
    ll_array_type = llvm::StructType::create(
        ctx, 
        { llvm::PointerType::get(ctx, 0), llvm::Type::getInt64Ty(ctx) }, 
        "_array"
    );
}

void CodeGenerator::getBuiltinFuncs() {
    // Generate the module's init function signature.
    ll_init_func = mod.getFunction("__LibBerry_Init");
    Assert(ll_init_func != nullptr, "missing __LibBerry_Init");

    llvm::BasicBlock::Create(ctx, "entry", ll_init_func);

    // Find the panic function for bounds checking.
    ll_panic_func = mod.getFunction("__LibBerry_Panic");
    Assert(ll_panic_func != nullptr, "missing __LibBerry_Panic");
}

void CodeGenerator::finishModule() {
    // Close the body the init func.
    auto& last_block = ll_init_func->back();   
    setCurrentBlock(&last_block);
    irb.CreateRetVoid();

    // Finalize all the debug information.
    debug.FinishModule();

    // Verify the module.
    std::string err_msg;
    llvm::raw_string_ostream oss(err_msg);
    if (llvm::verifyModule(mod, &oss)) {
        std::cerr << "error: verifying module:\n\n";
        std::cerr << err_msg << "\n\n";
        std::cerr << "printing module:\n\n";
        mod.print(llvm::errs(), nullptr);
        exit(1);
    }
}

/* -------------------------------------------------------------------------- */

llvm::Type* CodeGenerator::genType(Type* type) {
    type = type->Inner();

    switch (type->kind) {
    case TYPE_BOOL:
        return llvm::Type::getInt1Ty(ctx);
    case TYPE_UNIT:
        // TODO: handle in L-values...
        return llvm::Type::getVoidTy(ctx);
    case TYPE_INT: {
        switch (type->ty_Int.bit_size) {
        case 8:
            return llvm::Type::getInt8Ty(ctx);
        case 16:
            return llvm::Type::getInt16Ty(ctx);
        case 32:
            return llvm::Type::getInt32Ty(ctx);
        case 64:
            return llvm::Type::getInt64Ty(ctx);
        }
    } break;
    case TYPE_FLOAT: {
        switch (type->ty_Float.bit_size) {
        case 32:
            return llvm::Type::getFloatTy(ctx);
        case 64:
            return llvm::Type::getDoubleTy(ctx);
        }
    } break;
    case TYPE_PTR: 
        return llvm::PointerType::get(ctx, 0);
    case TYPE_FUNC: {
        auto& func_type = type->ty_Func;

        std::vector<llvm::Type*> ll_param_types;
        for (auto* param_type : func_type.param_types) {
            ll_param_types.push_back(genType(param_type));
        }

        return llvm::FunctionType::get(genType(func_type.return_type), ll_param_types, false);
    } break;
    case TYPE_ARRAY: 
        return ll_array_type;
    case TYPE_UNTYP:
        Panic("abstract untyped in codegen");
        break;
    }

    Panic("unimplemented type in codegen");
    return nullptr;
}

/* -------------------------------------------------------------------------- */

LoopContext& CodeGenerator::getLoopCtx() {
    Assert(loop_ctx_stack.size() > 0, "loop control statement missing loop context in codegen");
    return loop_ctx_stack.back();
}

void CodeGenerator::pushLoopContext(llvm::BasicBlock* break_block, llvm::BasicBlock* continue_block) {
    loop_ctx_stack.emplace_back(break_block, continue_block);
}

void CodeGenerator::popLoopContext() {
    Assert(loop_ctx_stack.size() > 0, "pop on empty loop context stack in codegen");
    loop_ctx_stack.pop_back();
}

/* -------------------------------------------------------------------------- */

llvm::BasicBlock* CodeGenerator::getCurrentBlock() {
    return irb.GetInsertBlock();
}

void CodeGenerator::setCurrentBlock(llvm::BasicBlock* block) {
    irb.SetInsertPoint(block);
}

llvm::BasicBlock* CodeGenerator::appendBlock() {
    Assert(ll_enclosing_func != nullptr, "append basic block without enclosing function");

    return llvm::BasicBlock::Create(ctx, "", ll_enclosing_func);
}

bool CodeGenerator::currentHasTerminator() {
    return getCurrentBlock()->getTerminator() != nullptr;
}

bool CodeGenerator::hasPredecessor(llvm::BasicBlock* block) {
    if (block == nullptr) 
        block = getCurrentBlock();

    return block->hasNPredecessorsOrMore(1);
}

void CodeGenerator::deleteCurrentBlock(llvm::BasicBlock* new_current) {
    auto* old_current = getCurrentBlock();
    setCurrentBlock(new_current);
    
    old_current->eraseFromParent();
}

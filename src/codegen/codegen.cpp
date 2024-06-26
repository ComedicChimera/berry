#include "codegen.hpp"

#include <iostream>

#include "llvm/IR/Verifier.h"

void CodeGenerator::GenerateModule() {
    createBuiltinGlobals();

    genImports();

    for (auto& file : src_mod.files) {
        debug.EmitFileInfo(file);
    }

    for (auto* decl : src_mod.decls) {
        src_file = &src_mod.files[decl->file_num];

        genDeclProto(decl);
    }

    genBuiltinFuncs();

    for (auto* decl : src_mod.decls) {
        src_file = &src_mod.files[decl->file_num];
        debug.SetCurrentFile(*src_file);

        genDeclBody(decl);
    }  

    finishModule();    
}

/* -------------------------------------------------------------------------- */

void CodeGenerator::createBuiltinGlobals() {
    // Set platform integer types.
    auto bit_size = layout.getPointerSizeInBits();
    Assert(bit_size == platform_int_type->ty_Int.bit_size, "mismatch between compiler and LLVM platform int bitsize");
    ll_platform_int_type = llvm::IntegerType::get(ctx, bit_size);

    // Declare the global array type.
    ll_slice_type = llvm::StructType::create(
        ctx, 
        { llvm::PointerType::get(ctx, 0), llvm::Type::getInt64Ty(ctx) }, 
        "_slice"
    );

    // Set the global runtime stub type (void function accepting no arguments).
    ll_rtstub_void_type = llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), false);
}

void CodeGenerator::genBuiltinFuncs() {
    // Generate the module's init function signature.
    ll_init_func = llvm::Function::Create(
        ll_rtstub_void_type, 
        llvm::Function::ExternalLinkage, 
        std::format("__berry_initmod${}", src_mod.id), 
        mod
    );
    ll_init_block = llvm::BasicBlock::Create(ctx, "entry", ll_init_func);

    // Generate the regular runtime stubs.
    rtstub_strcmp = mod.getFunction("__berry_strcmp");
    if (rtstub_strcmp == nullptr) {
        rtstub_strcmp = llvm::Function::Create(
            llvm::FunctionType::get(
                ll_platform_int_type,
                { ll_slice_type, ll_slice_type },
                false
            ),
            llvm::Function::ExternalLinkage,
            "__berry_strcmp",
            mod
        );
    }

    rtstub_strhash = mod.getFunction("__berry_strhash");
    if (rtstub_strhash == nullptr) {
        rtstub_strhash = llvm::Function::Create(
            llvm::FunctionType::get(
                ll_platform_int_type,
                { ll_slice_type },
                false
            ),
            llvm::Function::ExternalLinkage,
            "__berry_strhash",
            mod
        );
    }
}

llvm::Function* CodeGenerator::genPanicStub(const std::string& stub_name) {
    auto* stub_func = mod.getFunction(stub_name);

    if (stub_func == nullptr) {
        return llvm::Function::Create(
            ll_rtstub_void_type,
            llvm::Function::ExternalLinkage,
            stub_name,
            mod
        );
    }

    return stub_func;
}

void CodeGenerator::finishModule() {
    // Close the body the init func.
    setCurrentBlock(ll_init_block);

    // Call user specified init function if there is any.
    auto it = src_mod.symbol_table.find("init");
    if (it != src_mod.symbol_table.end()) {
        auto sym = it->second;
        if (
            (sym->flags & SYM_FUNC) && 
            sym->type->kind == TYPE_FUNC &&
            sym->type->ty_Func.param_types.size() == 0 && 
            sym->type->ty_Func.return_type->kind == TYPE_UNIT
        ) {
            irb.CreateCall(
                llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), false), 
                sym->llvm_value
            );
        }
    }

    // End the init block.
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

    // Add the init function call to rt_main.
    mainb.GenInitCall(ll_init_func);
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

/* -------------------------------------------------------------------------- */

llvm::Type* CodeGenerator::genType(Type* type, bool alloc_type) {
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
    case TYPE_FUNC: 
        return llvm::PointerType::get(ctx, 0);
    case TYPE_ARRAY:
        if (alloc_type) {
            return llvm::ArrayType::get(
                genType(type->ty_Array.elem_type, true),
                type->ty_Array.len
            );
        }

        return llvm::PointerType::get(ctx, 0);
    case TYPE_SLICE: 
    case TYPE_STRING:
        return ll_slice_type;
    case TYPE_STRUCT:
        return genNamedBaseType(type, alloc_type, "");     
    case TYPE_NAMED:
        return genNamedBaseType(type->ty_Named.type, alloc_type, type->ty_Named.name);
    case TYPE_ENUM:
        return ll_platform_int_type;
    case TYPE_UNTYP:
        Panic("abstract untyped in codegen");
        break;
    }

    Panic("unimplemented type in codegen");
    return nullptr;
}

llvm::Type* CodeGenerator::genNamedBaseType(Type* type, bool alloc_type, std::string_view type_name) {
    switch (type->kind) {
    case TYPE_STRUCT:
        if (alloc_type || !shouldPtrWrap(type)) {
            if (type->ty_Struct.llvm_type == nullptr) {
                std::vector<llvm::Type*> field_types(type->ty_Struct.fields.size());
                for (size_t i = 0; i < field_types.size(); i++) {
                    field_types[i] = genType(type->ty_Struct.fields[i].type, true);
                }

                if (type_name.size() == 0) {
                    type->ty_Struct.llvm_type = llvm::StructType::get(ctx, field_types);
                } else {
                    type->ty_Struct.llvm_type = llvm::StructType::create(ctx, field_types, mangleName(type_name));
                }
            } 
            
            return type->ty_Struct.llvm_type;
        }

        return llvm::PointerType::get(ctx, 0);
    case TYPE_ENUM:
        return ll_platform_int_type;  
    default:
        Panic("bad type to call genNamedBaseType in codegen");
        return nullptr;
    }
}

bool CodeGenerator::shouldPtrWrap(Type* type) {
    type = type->Inner();

    if (type->kind == TYPE_ARRAY) {
        return true;
    } else if (type->kind == TYPE_NAMED || type->kind == TYPE_STRUCT) {
        return shouldPtrWrap(genType(type, true));
    }

    return false;
}

bool CodeGenerator::shouldPtrWrap(llvm::Type* type) {
    if (llvm::ArrayType::classof(type))
        return true;

    return getLLVMByteSize(type) > layout.getPointerSize() * 2;
}

uint64_t CodeGenerator::getLLVMByteSize(llvm::Type* llvm_type) {
    return layout.getTypeAllocSize(llvm_type).getFixedSize();
}

uint64_t CodeGenerator::getLLVMByteAlign(llvm::Type* llvm_type) {
    return layout.getPrefTypeAlign(llvm_type).value();
}
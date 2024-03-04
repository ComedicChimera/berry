#ifndef CODEGEN_H_INC
#define CODEGEN_H_INC

#include "llvm/IR/Module.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/DIBuilder.h"

#include "ast.hpp"

// DebugGenerator generates debug information for a Berry module.  It is called
// by the CodeGenerator and acts as its "companion".
class DebugGenerator {
    bool no_emit;
    
    llvm::Module& mod;
    llvm::IRBuilder<>& irb;

    llvm::DIBuilder db;
    std::vector<llvm::DIScope*> lexical_blocks;

    llvm::DIFile* curr_file;

    llvm::DIType* prim_type_table[16];

    int disable_count;

public:
    DebugGenerator(bool should_emit, llvm::Module& mod, llvm::IRBuilder<>& irb)
    : no_emit(!should_emit)
    , mod(mod)
    , irb(irb)
    , db(mod)
    , curr_file(nullptr)
    {
        buildTypeTable();

        disable_count = should_emit ? 0 : -1;
    }

    /* ---------------------------------------------------------------------- */

    void PushDisable();
    void PopDisable();

    /* ---------------------------------------------------------------------- */

    void EmitFileInfo(SourceFile& src_file);
    void SetCurrentFile(SourceFile &src_file);
    void FinishModule();

    /* ---------------------------------------------------------------------- */

    void BeginFuncBody(AstDef* fd, llvm::Function* ll_func);
    void EndFuncBody();
    void EmitGlobalVariableInfo(AstDef* node, llvm::GlobalVariable* ll_gv);
    void EmitLocalVariableInfo(AstStmt* node, llvm::Value* ll_var);

    /* ---------------------------------------------------------------------- */

    void SetDebugLocation(const TextSpan &span);
    void ClearDebugLocation();
    llvm::DILocation* GetDebugLoc(llvm::DIScope* scope, const TextSpan& span);

    /* ---------------------------------------------------------------------- */

    llvm::DIType *GetDIType(Type *type, uint call_conv = llvm::dwarf::DW_CC_normal);

private:
    void buildTypeTable();
};

// CodeGenerator compiles a Berry module to an LLVM module.
class CodeGenerator {
    // ctx is the LLVM context being compiled in.
    llvm::LLVMContext& ctx;

    // mod is the LLVM module being generated.
    llvm::Module& mod;

    // layout is the module's data layout (retrieved for convenience).
    const llvm::DataLayout& layout;

    // builder is the IR builder being used.
    llvm::IRBuilder<> irb;
    
    // debug is the debug generator instance.
    DebugGenerator debug;

    // bry_mod is the Berry module being compiled.
    Module& bry_mod;

    /* ---------------------------------------------------------------------- */

    // ll_enclosing_func is the enclosing LLVM function.
    llvm::Function* ll_enclosing_func;

    // return_param is the return parameter (if present).
    llvm::Value* return_param;

    // var_block is the block to append variable allocas to.
    llvm::BasicBlock* var_block;

    // tctx is a utility type context for the code generator (used for comparisons).
    TypeContext tctx;

    // LoopContext stores the jump locations for an enclosing loop.
    struct LoopContext {
        // break_block is the jump destination of a break statement.
        llvm::BasicBlock* break_block;

        // continue_block is the jump destination of a continue statement.
        llvm::BasicBlock* continue_block;
    };

    // loop_ctx_stack is the stack of enclosing loop contexts.
    std::vector<LoopContext> loop_ctx_stack;

    /* ---------------------------------------------------------------------- */

    // ll_array_type is the LLVM type for all Berry arrays (opaque pointer POG).
    llvm::StructType* ll_array_type;

    // ll_init_func is the module initialization function (where non-constant
    // global initializers are placed).  This is indirectly called by the
    // runtime at startup through `__berry_main`.
    llvm::Function* ll_init_func;

    // ll_init_block is the current block for appending in the init func.
    llvm::BasicBlock* ll_init_block;

    // ll_panic_func is the runtime out of bounds panic function.
    llvm::Function* ll_panic_oob_func;

    // ll_panic_badslice_func is the runtime bad slice panic function.
    llvm::Function* ll_panic_badslice_func;

    /* ---------------------------------------------------------------------- */

    // loaded_imports stores the imports that are loaded.  The first index is
    // the dependency ID and the second index is the export number.
    std::vector<std::unordered_map<size_t, llvm::Value*>> loaded_imports;

    // cconv_name_to_id maps Berry calling convention names to their LLVM IDs.
    std::unordered_map<std::string_view, llvm::CallingConv::ID> cconv_name_to_id {
        { "c", llvm::CallingConv::C },
        { "stdcall", llvm::CallingConv::X86_StdCall },
        { "win64", llvm::CallingConv::Win64 }
    };

public:
    // Creates a new code generator using ctx and outputting to mod.
    CodeGenerator(llvm::LLVMContext& ctx, llvm::Module& mod, Module& bry_mod, bool debug)
    : ctx(ctx), mod(mod), irb(ctx)
    , layout(mod.getDataLayout())
    , debug(debug, mod, irb)
    , bry_mod(bry_mod)
    , ll_enclosing_func(nullptr)
    , var_block(nullptr)
    , ll_array_type(nullptr)
    , ll_init_func(nullptr)
    , ll_init_block(nullptr)
    , ll_panic_oob_func(nullptr)
    , ll_panic_badslice_func(nullptr)
    , loaded_imports(bry_mod.deps.size())
    {}

    // GenerateModule compiles the module.
    void GenerateModule();

private:
    void genImports();
    llvm::Value* genImportFunc(Module &imported_mod, AstDef* node);
    llvm::Value* genImportGlobalVar(Module &imported_mod, AstDef* node);

    void genTopDecl(AstDef* def);
    void genPredicates(AstDef* def);

    void genFuncProto(AstDef* node);
    void genFuncBody(AstDef* node);

    void genGlobalVarDecl(AstDef* node);
    void genGlobalVarInit(AstDef* node);

    std::string mangleName(std::string_view name);
    std::string mangleName(Module &imported_bry_mod, std::string_view name);

    /* ---------------------------------------------------------------------- */

    void genStmt(AstStmt* stmt);
    void genIfTree(AstStmt* node);
    void genWhileLoop(AstStmt* node);
    void genForLoop(AstStmt* node);
    void genLocalVar(AstStmt* node);
    void genAssign(AstStmt* node);
    void genIncDec(AstStmt* node);

    /* ---------------------------------------------------------------------- */

    void genStoreExpr(AstExpr* node, llvm::Value* dest);
    void genStructCopy(llvm::Type* llvm_struct_type, llvm::Value* src, llvm::Value* dest);
    llvm::Value* genStackAlloc(Type* type);

    llvm::Value* genExpr(AstExpr* expr, bool expect_addr = false, llvm::Value* alloc_loc = nullptr);
    llvm::Value* genCast(AstExpr* node);
    llvm::Value* genBinop(AstExpr* node);
    llvm::Value* genUnop(AstExpr* node);

    /* ---------------------------------------------------------------------- */

    llvm::Value* genCall(AstExpr* node, llvm::Value* alloc_loc);
    llvm::Value* genIndexExpr(AstExpr* node, bool expect_addr);
    llvm::Value* genSliceExpr(AstExpr* node, llvm::Value* alloc_loc);
    llvm::Value* genFieldExpr(AstExpr* node, bool expect_addr);
    llvm::Value* genArrayLit(AstExpr* node, llvm::Value* alloc_loc);
    llvm::Value* genNewExpr(AstExpr* node, llvm::Value* alloc_loc);
    llvm::Value* genStructLit(AstExpr* node, llvm::Value* alloc_loc);
    llvm::Value* genStrLit(AstExpr *node, llvm::Value *alloc_loc);
    llvm::Value* genIdent(AstExpr* node, bool expect_addr);

    /* ---------------------------------------------------------------------- */

    void createBuiltinGlobals();
    void genRuntimeStubs();
    void finishModule();

    /* ---------------------------------------------------------------------- */

    // genType converts the Berry type type to an LLVM type.
    llvm::Type* genType(Type* type, bool alloc_type = false);
    llvm::Type* genNamedBaseType(Type* type, bool alloc_type, std::string_view type_name);
    bool shouldPtrWrap(Type* type);
    bool shouldPtrWrap(llvm::Type* type);
    uint64_t getLLVMTypeByteSize(llvm::Type* llvm_type);

    /* ---------------------------------------------------------------------- */

    llvm::BasicBlock *getCurrentBlock();
    void setCurrentBlock(llvm::BasicBlock *block);
    llvm::BasicBlock *appendBlock();
    bool currentHasTerminator();
    bool hasPredecessor(llvm::BasicBlock* block = nullptr);
    void deleteCurrentBlock(llvm::BasicBlock* new_current);

    /* ---------------------------------------------------------------------- */

    llvm::Value* getArrayData(llvm::Value* array);
    llvm::Value* getArrayLen(llvm::Value *array);
    llvm::Value* getArrayDataPtr(llvm::Value* array_ptr);
    llvm::Value *getArrayLenPtr(llvm::Value* array_ptr);
    void genBoundsCheck(llvm::Value* ndx, llvm::Value* arr_len, bool can_equal_len = false);

    /* ---------------------------------------------------------------------- */

    llvm::Constant *getNullValue(Type *type);
    llvm::Constant* getNullValue(llvm::Type* ll_type);
    llvm::Constant* getInt32Const(uint32_t value);
    llvm::Constant* getInt8Const(uint8_t value);
    llvm::Constant* getPlatformIntConst(uint64_t value);
    llvm::Value* makeLLVMIntLit(Type *int_type, uint64_t value);
    llvm::Value* makeLLVMFloatLit(Type *float_type, double value);

    /* ---------------------------------------------------------------------- */

    LoopContext& getLoopCtx();
    void pushLoopContext(llvm::BasicBlock *break_block, llvm::BasicBlock* continue_block);
    void popLoopContext();
};

#endif
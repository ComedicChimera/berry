#ifndef CODEGEN_H_INC
#define CODEGEN_H_INC

#include "llvm/IR/Module.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/DIBuilder.h"

#include "hir.hpp"

class MainBuilder {
    llvm::LLVMContext& ctx;
    llvm::Module& main_mod;

    llvm::Function* rt_main_func;
    llvm::FunctionType* rt_stub_func_type;
    llvm::IRBuilder<> irb;

public:
    MainBuilder(llvm::LLVMContext& ctx, llvm::Module& main_mod);
    void GenInitCall(llvm::Function* init_func);
    void GenUserMainCall(Module& root_mod);
    void FinishMain();
};


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

    void BeginFuncBody(Decl* decl, llvm::Function* ll_func);
    void EndFuncBody();
    void EmitGlobalVariableInfo(Decl* decl, llvm::GlobalVariable* ll_gv);
    void EmitLocalVariableInfo(HirStmt* node, llvm::Value* ll_var);

    /* ---------------------------------------------------------------------- */

    void SetDebugLocation(const TextSpan &span);
    void ClearDebugLocation();
    llvm::DILocation* GetDebugLoc(llvm::DIScope* scope, const TextSpan& span);

    /* ---------------------------------------------------------------------- */

    llvm::DIType *GetDIType(Type *type, uint call_conv = llvm::dwarf::DW_CC_normal);

private:
    void buildTypeTable();
};

/* -------------------------------------------------------------------------- */

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

    // src_mod is the source module being compiled.
    Module& src_mod;

    // src_file is the source file whose definition is being processed.
    SourceFile* src_file;

    // mainb is the main builder for the compilation task.
    MainBuilder& mainb;

    // arena is the arena used by the code generator.
    Arena& arena;

    /* ---------------------------------------------------------------------- */

    // ll_enclosing_func is the enclosing LLVM function.
    llvm::Function* ll_enclosing_func { nullptr };

    // return_param is the return parameter (if present).
    llvm::Value* return_param { nullptr };

    // var_block is the block to append variable allocas to.
    llvm::BasicBlock* var_block { nullptr };

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

    // fallthru_stack is the stack of enclosing fallthrough destinations.
    std::vector<llvm::BasicBlock*> fallthru_stack;

    /* ---------------------------------------------------------------------- */

    // ll_platform_int_type is the native-width integer type for the target
    // platform.  This will be i64 for 64-bit and i32 for 32-bit.
    llvm::IntegerType* ll_platform_int_type { nullptr };

    // ll_slice_type is the LLVM type for all Berry slices (opaque pointer POG).
    llvm::StructType* ll_slice_type { nullptr };
    
    // ll_rtstub_void_type is the function type for runtime stubs that do not
    // take or return values (ex: panic functions, init module, etc.)
    llvm::FunctionType* ll_rtstub_void_type { nullptr };

    /* ---------------------------------------------------------------------- */

    // ll_init_func is the module initialization function (where non-constant
    // global initializers are placed).  This is indirectly called by the
    // runtime at startup through `__berry_main`.
    llvm::Function* ll_init_func { nullptr };

    // ll_init_block is the current block for appending in the init func.
    llvm::BasicBlock* ll_init_block { nullptr };

    /* ---------------------------------------------------------------------- */

    // Runtime Stubs
    llvm::Function* rtstub_panic_oob { nullptr };
    llvm::Function* rtstub_panic_badslice { nullptr };
    llvm::Function* rtstub_panic_unreachable { nullptr };
    llvm::Function* rtstub_panic_divide { nullptr };
    llvm::Function* rtstub_panic_overflow { nullptr };
    llvm::Function* rtstub_panic_shift { nullptr };
    llvm::Function* rtstub_strcmp { nullptr };
    llvm::Function* rtstub_strhash { nullptr };

    /* ---------------------------------------------------------------------- */

    // loaded_imports stores the imports that are loaded.  The first index is
    // the dependency ID and the second index is the definition number.
    std::vector<std::unordered_map<size_t, llvm::Value*>> loaded_imports;

    /* ---------------------------------------------------------------------- */

    // cconv_name_to_id maps Berry calling convention names to their LLVM IDs.
    std::unordered_map<std::string_view, llvm::CallingConv::ID> cconv_name_to_id {
        { "c", llvm::CallingConv::C },
        { "stdcall", llvm::CallingConv::X86_StdCall },
        { "win64", llvm::CallingConv::Win64 }
    };

public:
    // Creates a new code generator using ctx and outputting to mod.
    CodeGenerator(
        llvm::LLVMContext& ctx, 
        llvm::Module& mod, 
        Module& src_mod, 
        bool debug,
        MainBuilder& mainb,
        Arena& arena
    )
    : ctx(ctx), mod(mod), src_mod(src_mod), debug(debug, mod, irb)
    , mainb(mainb), arena(arena)
    , irb(ctx)
    , layout(mod.getDataLayout())
    , loaded_imports(src_mod.deps.size())
    {}

    // GenerateModule compiles the module.
    void GenerateModule();

private:
    void createBuiltinGlobals();
    void genBuiltinFuncs();
    llvm::Function* genPanicStub(const std::string& stub_name);
    void finishModule();

    /* ---------------------------------------------------------------------- */

    void genImports();
    llvm::Value* genImportFunc(Module &imported_mod, Decl* decl);
    llvm::Value* genImportMethod(Module& imported_mod, Decl* decl);
    llvm::Value* genImportFactory(Module& imported_mod, Decl* decl);
    llvm::Value* genImportGlobalVar(Module& imported_mod, Decl* decl);

    void genDeclProto(Decl* decl);
    void genDeclBody(Decl* decl);

    void genFuncProto(Decl* decl);
    void genMethodProto(Decl* decl);
    void genFactoryProto(Decl* decl);
    llvm::FunctionType* genFuncType(Type* type, bool has_self_ptr = false);

    void genFuncBody(Decl* decl);
    void genMethodBody(Decl* decl);
    void genFactoryBody(Decl* decl);
    void genInnerFuncBody(Type* return_type, llvm::Function* ll_func, std::span<Symbol*> params, HirStmt* body);

    void genGlobalVarDecl(Decl* decl);
    void genGlobalVarInit(HirDecl* node);
    void genGlobalConst(Decl* decl);

    std::string mangleName(std::string_view name);
    std::string mangleName(Module &imported_bry_mod, std::string_view name);

    /* ---------------------------------------------------------------------- */

    enum {
        CTG_NONE = 0,
        CTG_CONST = 1,
        CTG_EXPORTED = 2,
        CTG_UNWRAPPED = 4
    };
    typedef int ComptimeGenFlags;

    llvm::Constant* genComptime(ConstValue* value, ComptimeGenFlags flags, Type* expect_type);
    llvm::Constant* genComptimeArray(ConstValue* value, ComptimeGenFlags flags, Type* expect_type);
    llvm::Constant* genComptimeZeroArray(ConstValue* value, ComptimeGenFlags flags, Type* expect_type);
    llvm::Constant* genComptimeString(ConstValue* value, ComptimeGenFlags flags);
    llvm::Constant* genComptimeStruct(ConstValue* value, ComptimeGenFlags flags, Type* expect_type);
    llvm::Constant* genComptimeInnerStruct(ConstValue *value, ComptimeGenFlags flags, Type* expect_type);

    /* ---------------------------------------------------------------------- */

    void genStmt(HirStmt* node);
    void genIfTree(HirStmt* node);
    void genWhileLoop(HirStmt* node);
    void genForLoop(HirStmt* node);
    void genMatchStmt(HirStmt* node);
    void genCpdAssign(HirStmt* node);
    void genIncDec(HirStmt* node);

    LoopContext& getLoopCtx();
    void pushLoopContext(llvm::BasicBlock* break_block, llvm::BasicBlock* continue_block);
    void popLoopContext();

    /* ---------------------------------------------------------------------- */

    struct PatternBranch {
        HirExpr* pattern;
        llvm::BasicBlock* block;

        PatternBranch(HirExpr* pattern_, llvm::BasicBlock* block_)
        : pattern(pattern_)
        , block(block_)
        {}
    };

    void genPatternMatch(HirExpr* expr, const std::vector<PatternBranch>& pcases, llvm::BasicBlock* nm_block);
    bool pmAddCase(llvm::SwitchInst *pswitch, llvm::Value *match_operand, HirExpr *pattern, llvm::BasicBlock *case_block);
    void pmGenCapture(Symbol* capture_sym, llvm::Value* match_operand, llvm::BasicBlock* case_block);

    void pmGenStrMatch(llvm::Value *match_operand, const std::vector<PatternBranch> &pcases, llvm::BasicBlock *nm_block);
    typedef std::unordered_map<size_t, std::vector<PatternBranch>> PatternBuckets;
    llvm::BasicBlock* pmAddStringCase(PatternBuckets& buckets, llvm::Value* match_operand, HirExpr* pattern, llvm::BasicBlock* case_block);

    /* ---------------------------------------------------------------------- */

    llvm::Value* genExpr(HirExpr* expr, bool expect_addr = false, llvm::Value* alloc_loc = nullptr);

    llvm::Value* genAtomicCas(HirExpr* node);

    llvm::Value* genTestMatch(HirExpr* node);

    llvm::Value* genCast(HirExpr *node);
    llvm::Value* genBinop(HirExpr* node);
    llvm::Value* genStrEq(llvm::Value* lhs, llvm::Value* rhs);
    llvm::Value* genUnop(HirExpr* node);

    void genDivideByZeroCheck(llvm::Value* divisor, Type* int_type);
    void genDivideOverflowCheck(llvm::Value* dividend, llvm::Value* divisor, Type* int_type);
    void genShiftOverflowCheck(llvm::Value* rhs, Type* int_type);
    llvm::Value* genLLVMExpect(llvm::Value* value, llvm::Value* expected);

    /* ---------------------------------------------------------------------- */

    llvm::Value* genCall(HirExpr* node, llvm::Value* alloc_loc);
    llvm::Value* genCallMethod(HirExpr* node, llvm::Value* alloc_loc);
    llvm::Value* genCallFactory(HirExpr* node, llvm::Value* alloc_loc);

    llvm::Value* genIndexExpr(HirExpr* node, bool expect_addr);
    llvm::Value* genSliceExpr(HirExpr* node, llvm::Value* alloc_loc);
    llvm::Value* genFieldExpr(HirExpr* node, bool expect_addr);

    llvm::Value* genNewExpr(HirExpr* node);
    llvm::Value* genNewArray(HirExpr* node, llvm::Value* alloc_loc);
    llvm::Value* genNewStruct(HirExpr* node);
    llvm::Value* genArrayLit(HirExpr* node, llvm::Value* alloc_loc);
    llvm::Value* genStructLit(HirExpr* node, llvm::Value* alloc_loc);
    llvm::Value* genStringLit(HirExpr* node, llvm::Value* alloc_loc);
    llvm::Value* genIdent(HirExpr* node, bool expect_addr);

    /* ---------------------------------------------------------------------- */

    void genStoreExpr(HirExpr* node, llvm::Value* dest);
    void genMemCopy(llvm::Type* ll_type, llvm::Value* src, llvm::Value* dest);

    inline llvm::Value* genAlloc(Type* type, HirAllocMode mode) { return genAlloc(genType(type, true), mode); }
    llvm::Value* genAlloc(llvm::Type* llvm_type, HirAllocMode mode);

    void genBoundsCheck(llvm::Value* ndx, llvm::Value* arr_len, bool can_equal_len = false);
    
    llvm::Value* getSliceData(llvm::Value* array);
    llvm::Value* getSliceLen(llvm::Value *array);
    llvm::Value* getSliceDataPtr(llvm::Value* array_ptr);
    llvm::Value* getSliceLenPtr(llvm::Value* array_ptr);
    
    llvm::Constant* getNullValue(Type *type);
    llvm::Constant* getNullValue(llvm::Type* ll_type);
    llvm::Constant* getInt32Const(uint32_t value);
    llvm::Constant* getInt8Const(uint8_t value);
    llvm::Constant* getPlatformIntConst(uint64_t value);
    llvm::Constant* makeLLVMIntLit(Type *int_type, uint64_t value);
    llvm::Constant* makeLLVMFloatLit(Type *float_type, double value);
    std::string decodeStrLit(std::string_view lit_val);

    /* ---------------------------------------------------------------------- */

    llvm::BasicBlock* getCurrentBlock();
    void setCurrentBlock(llvm::BasicBlock *block);
    llvm::BasicBlock* appendBlock();
    bool currentHasTerminator();
    bool hasPredecessor(llvm::BasicBlock* block = nullptr);
    void deleteCurrentBlock(llvm::BasicBlock* new_current);

    /* ---------------------------------------------------------------------- */

    // genType converts the Berry type type to an LLVM type.
    llvm::Type* genType(Type* type, bool alloc_type = false);
    llvm::Type* genNamedBaseType(Type* type, bool alloc_type, std::string_view type_name);
    bool shouldPtrWrap(Type* type);
    bool shouldPtrWrap(llvm::Type* type);
    uint64_t getLLVMByteSize(llvm::Type* llvm_type);
    uint64_t getLLVMByteAlign(llvm::Type* llvm_type);
};

#endif
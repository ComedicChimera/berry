#ifndef CODEGEN_H_INC
#define CODEGEN_H_INC

#include "llvm/IR/Module.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/DIBuilder.h"

#include "ast.hpp"

enum ConstKind {
    CONST_I8,
    CONST_U8,
    CONST_I16,
    CONST_U16,
    CONST_I32,
    CONST_U32,
    CONST_I64,
    CONST_U64,
    CONST_F32,
    CONST_F64,
    CONST_BOOL,
    CONST_PTR,
    CONST_FUNC,
    CONST_ARRAY,
    CONST_ZERO_ARRAY,
    CONST_STRING,
    CONST_STRUCT,

    CONSTS_COUNT
};

struct ConstValue {
    ConstKind kind;
    union {
        int8_t v_i8;
        uint8_t v_u8;
        int16_t v_i16 ;
        uint16_t v_u16;
        int32_t v_i32;
        uint32_t v_u32;
        int64_t v_i64;
        uint64_t v_u64;
        float v_f32 ;
        double v_f64 ;
        bool v_bool;
        size_t v_ptr;
        Symbol* v_func;
        struct {
            std::span<ConstValue*> elems;
            Type* elem_type;
            size_t mod_id;
            llvm::Constant* alloc_loc;
        } v_array;
        struct {
            size_t num_elems;
            Type* elem_type;
            size_t mod_id;
            llvm::Constant* alloc_loc;
        } v_zarr;
        struct {
            std::string_view value;
            size_t mod_id;
            llvm::Constant* alloc_loc;
        } v_str;
        struct {
            std::span<ConstValue*> fields;
            Type* struct_type;
            size_t mod_id;
            llvm::Constant* alloc_loc;
        } v_struct;
    };
};

/* -------------------------------------------------------------------------- */

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
    // the dependency ID and the second index is the definition number.
    std::vector<std::unordered_map<size_t, llvm::Value*>> loaded_imports;

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
    , ll_enclosing_func(nullptr)
    , var_block(nullptr)
    , ll_array_type(nullptr)
    , ll_init_func(nullptr)
    , ll_init_block(nullptr)
    , ll_panic_oob_func(nullptr)
    , ll_panic_badslice_func(nullptr)
    , loaded_imports(src_mod.deps.size())
    {}

    // GenerateModule compiles the module.
    void GenerateModule();

private:
    void createBuiltinGlobals();
    void genRuntimeStubs();
    void finishModule();

    /* ---------------------------------------------------------------------- */

    void genImports();
    llvm::Value* genImportFunc(Module &imported_mod, AstDef* node);
    llvm::Value* genImportGlobalVar(Module &imported_mod, AstDef* node);

    void genTopDecl(AstDef* def);
    void genPredicates(AstDef* def);

    void genFuncProto(AstDef* node);
    void genFuncBody(AstDef* node);

    void genGlobalVarDecl(AstDef* node);
    void genGlobalVarInit(AstDef *node);

    std::string mangleName(std::string_view name);
    std::string mangleName(Module &imported_bry_mod, std::string_view name);

    /* ---------------------------------------------------------------------- */
    
    ConstValue* evalComptime(AstExpr* node);
    ConstValue* evalComptimeCast(AstExpr* node);
    ConstValue* evalComptimeBinaryOp(AstExpr* node);
    ConstValue* evalComptimeUnaryOp(AstExpr* node);
    ConstValue* evalComptimeStructLitPos(AstExpr* node);
    ConstValue* evalComptimeStructLitNamed(AstExpr* node);
    ConstValue* evalComptimeSlice(AstExpr* node);
    uint64_t evalComptimeIndexValue(AstExpr* node, uint64_t len);
    bool evalComptimeSize(AstExpr* node, uint64_t* out_size);
    void comptimeEvalError(const TextSpan& span, const std::string& message);

    ConstValue* getComptimeNull(Type *type);
    ConstValue* allocComptime(ConstKind kind);

    enum {
        CTG_NONE = 0,
        CTG_CONST = 1,
        CTG_EXPORTED = 2,
        CTG_UNWRAPPED = 4
    };
    typedef int ComptimeGenFlags;

    llvm::Constant* genComptime(ConstValue* value, ComptimeGenFlags flags);
    llvm::Constant* genComptimeArray(ConstValue* value, ComptimeGenFlags flags);
    llvm::Constant* genComptimeZeroArray(ConstValue* value, ComptimeGenFlags flags);
    llvm::Constant* genComptimeString(ConstValue* value, ComptimeGenFlags flags);
    llvm::Constant* genComptimeStruct(ConstValue* value, ComptimeGenFlags flags);
    llvm::Constant* genComptimeInnerStruct(ConstValue *value, ComptimeGenFlags flags);

    /* ---------------------------------------------------------------------- */

    void genStmt(AstStmt* stmt);
    void genIfTree(AstStmt* node);
    void genWhileLoop(AstStmt* node);
    void genForLoop(AstStmt* node);
    void genLocalVar(AstStmt* node);
    void genAssign(AstStmt* node);
    void genIncDec(AstStmt* node);

    LoopContext& getLoopCtx();
    void pushLoopContext(llvm::BasicBlock *break_block, llvm::BasicBlock* continue_block);
    void popLoopContext();

    /* ---------------------------------------------------------------------- */

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
    llvm::Value *genStrLit(AstExpr *node, llvm::Value *alloc_loc);
    llvm::Value* genIdent(AstExpr* node, bool expect_addr);

    /* ---------------------------------------------------------------------- */

    void genStoreExpr(AstExpr* node, llvm::Value* dest);
    void genStructCopy(llvm::Type* llvm_struct_type, llvm::Value* src, llvm::Value* dest);

    inline llvm::Value* genAlloc(Type* type, AstAllocMode mode) { return genAlloc(genType(type, true), mode); }
    llvm::Value* genAlloc(llvm::Type* llvm_type, AstAllocMode mode);

    llvm::Value* getArrayData(llvm::Value* array);
    llvm::Value* getArrayLen(llvm::Value *array);
    llvm::Value* getArrayDataPtr(llvm::Value* array_ptr);
    llvm::Value* getArrayLenPtr(llvm::Value* array_ptr);
    void genBoundsCheck(llvm::Value* ndx, llvm::Value* arr_len, bool can_equal_len = false);
    
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
    uint64_t getLLVMTypeByteSize(llvm::Type* llvm_type);
};

#endif
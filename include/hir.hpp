#ifndef HIR_H_INC
#define HIR_H_INC

#include "symbol.hpp"

enum HirKind {
    HIR_FUNC,
    HIR_GLOBAL_VAR,
    HIR_GLOBAL_CONST,
    HIR_STRUCT,         // uses ir_TypeDef
    HIR_ALIAS,          // uses ir_TypeDef 
    HIR_ENUM,           // uses ir_TypeDef

    HIR_BLOCK,
    HIR_IF,
    HIR_WHILE,
    HIR_DO_WHILE,       // uses ir_While
    HIR_FOR,
    HIR_MATCH,
    HIR_UNSAFE,         // uses ir_Block
    HIR_LOCAL_VAR,
    HIR_LOCAL_CONST,
    HIR_ASSIGN,
    HIR_CPD_ASSIGN,
    HIR_INCDEC,
    HIR_EXPR_STMT,
    HIR_RETURN,
    HIR_BREAK,
    HIR_CONTINUE,
    HIR_FALLTHRU,

    HIR_TEST_MATCH,
    HIR_CAST,
    HIR_BINOP,
    HIR_UNOP,
    HIR_ADDR,
    HIR_DEREF,
    HIR_CALL,
    HIR_INDEX,
    HIR_SLICE,
    HIR_FIELD,
    HIR_DEREF_FIELD,    // uses ir_Field
    HIR_NEW,
    HIR_NEW_ARRAY,
    HIR_NEW_STRUCT,     // uses ir_StructLit
    HIR_ARRAY_LIT,
    HIR_STRUCT_LIT,
    HIR_ENUM_LIT,
    HIR_STATIC_GET,
    HIR_IDENT,
    HIR_NUM_LIT,
    HIR_FLOAT_LIT,
    HIR_BOOL_LIT,
    HIR_STRING_LIT,
    HIR_NULL,
    HIR_MACRO_SIZEOF,
    HIR_MACRO_ALIGNOF,

    HIRS_COUNT
};

struct HirNode {
    HirKind kind;
    TextSpan span;
};

/* -------------------------------------------------------------------------- */

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
    CONST_ENUM,

    CONSTS_COUNT
};

struct ConstValue {
    ConstKind kind;
    union {
        int8_t v_i8;
        uint8_t v_u8;
        int16_t v_i16;
        uint16_t v_u16;
        int32_t v_i32;
        uint32_t v_u32;
        int64_t v_i64;
        uint64_t v_u64;
        float v_f32;
        double v_f64;
        bool v_bool;
        uint64_t v_ptr;
        Symbol* v_func;
        struct {
            std::span<ConstValue*> elems;
            Type* elem_type;
            size_t mod_id;
            llvm::Constant* alloc_loc;
        } v_array;
        struct {
            uint64_t num_elems;
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
            size_t mod_id;
            llvm::Constant* alloc_loc;
        } v_struct;
        uint64_t v_enum;
    };
};

/* -------------------------------------------------------------------------- */

enum HirOpKind {
    HIROP_ADD,
    HIROP_SUB,
    HIROP_MUL,
    HIROP_DIV,
    HIROP_MOD,
    HIROP_SHL,
    HIROP_SHR,
    HIROP_EQ,
    HIROP_NE,
    HIROP_LT,
    HIROP_GT,
    HIROP_LE,
    HIROP_GE,
    HIROP_BWAND,
    HIROP_BWOR,
    HIROP_BWXOR,
    HIROP_LGAND,
    HIROP_LGOR,

    HIROP_NEG,
    HIROP_NOT,
    HIROP_BWNEG
};

enum HirAllocMode {
    HIRMEM_STACK,
    HIRMEM_HEAP,
    HIRMEM_GLOBAL
};

struct HirExpr;

struct HirFieldInit {
    HirExpr* expr;
    size_t field_index;

    HirFieldInit()
    : expr(nullptr)
    , field_index(0)
    {}

    HirFieldInit(HirExpr* expr_, size_t field_index_)
    : expr(expr_)
    , field_index(field_index_)
    {}
};

struct HirExpr : public HirNode {
    Type* type;
    bool assignable;

    union {
        struct {
            HirExpr* expr;
            std::span<HirExpr*> patterns;
        } ir_TestMatch;
        struct {
            HirExpr* expr;
        } ir_Cast;
        struct {
            HirExpr* lhs;
            HirExpr* rhs;
            HirOpKind op;
        } ir_Binop;
        struct {
            HirExpr* expr;
            HirOpKind op;
        } ir_Unop;
        struct {
            HirExpr* expr;
        } ir_Addr;
        struct {
            HirExpr* expr;
        } ir_Deref;
        struct {
            HirExpr* func;
            std::span<HirExpr*> args;
            HirAllocMode alloc_mode;
        } ir_Call;
        struct {
            HirExpr* expr;
            HirExpr* index;
        } ir_Index;
        struct {
            HirExpr* expr;
            HirExpr* start_index;
            HirExpr* end_index;
        } ir_Slice;
        struct {
            HirExpr* expr;
            size_t field_index;
        } ir_Field;
        struct {
            Type* elem_type;
            HirAllocMode alloc_mode;
        } ir_New;
        struct {
            HirExpr* len;
            uint64_t const_len;
            HirAllocMode alloc_mode;
        } ir_NewArray;
        struct {
            std::span<HirExpr*> items;
            HirAllocMode alloc_mode;
        } ir_ArrayLit;
        struct {
            std::span<HirFieldInit> field_inits;
            HirAllocMode alloc_mode;
        } ir_StructLit;
        struct {
            size_t tag_value;
        } ir_EnumLit;
        struct {
            Symbol* imported_symbol;
            size_t dep_id;
        } ir_StaticGet;
        struct {
            Symbol* symbol;
        } ir_Ident;
        struct {
            uint64_t value;
        } ir_Num;
        struct {
            double value;
        } ir_Float;
        struct {
            bool value;
        } ir_Bool;
        struct {
            std::string_view value;
        } ir_String;

        struct {
            Type* arg;
        } ir_TypeMacro;
    };

    HirExpr() {}
};

/* -------------------------------------------------------------------------- */

struct HirStmt;

struct HirIfBranch {
    HirExpr* cond;
    HirStmt* body;

    HirIfBranch()
    : cond(nullptr)
    , body(nullptr)
    {}

    HirIfBranch(HirExpr* cond_, HirStmt* body_)
    : cond(cond_)
    , body(body_)
    {}
};

struct HirCaseBlock {
    std::span<HirExpr*> patterns;
    HirStmt* body;

    HirCaseBlock()
    : body(nullptr)
    {}

    HirCaseBlock(std::span<HirExpr*> patterns_)
    : patterns(patterns_)
    , body(nullptr)
    {}
};

struct HirStmt : public HirNode {
    union {
        struct {
            std::span<HirStmt*> stmts;
        } ir_Block;
        struct {
            std::span<HirIfBranch> branches;
            HirStmt* else_stmt;
        } ir_If;
        struct {
            HirExpr* cond;
            HirStmt* body;
            HirStmt* else_stmt;
        } ir_While;
        struct {
            HirStmt* iter_var;
            HirExpr* cond;
            HirStmt* update_stmt;
            HirStmt* body;
            HirStmt* else_stmt;
        } ir_For;
        struct {
            HirExpr* expr;
            std::span<HirCaseBlock> cases;
            bool is_implicit_exhaustive;
        } ir_Match;
        struct {
            Symbol* symbol;
            HirExpr* init;
        } ir_LocalVar;
        struct {
            Symbol* symbol;
            ConstValue* init;
        } ir_LocalConst;
        struct {
            HirExpr* lhs;
            HirExpr* rhs;
        } ir_Assign;
        struct {
            HirExpr* lhs;
            HirExpr* rhs;
            HirOpKind op;

            Type* binop_type;
            bool needs_subtype_cast;
        } ir_CpdAssign;
        struct {
            HirExpr* expr;
            HirOpKind op;

            Type* binop_type;
            bool needs_subtype_cast;
        } ir_IncDec;
        struct {
            HirExpr* expr;
        } ir_ExprStmt;
        struct {
            HirExpr* expr;
        } ir_Return;
    };

    HirStmt() {}
};

/* -------------------------------------------------------------------------- */

struct HirDecl : public HirNode {
    union {
        struct {
            Symbol* symbol;
            std::span<Symbol*> params;
            Type* return_type;
            HirStmt* body;
        } ir_Func;
        struct {
            Symbol* symbol;
            HirExpr* init;
            ConstValue* const_init;
        } ir_GlobalVar;
        struct {
            Symbol* symbol;
            ConstValue* init;
        } ir_GlobalConst;
        struct {
            Symbol* symbol;
        } ir_TypeDef;
    };

    HirDecl() {}
};

#endif
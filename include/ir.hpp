#ifndef IR_H_INC
#define IR_H_INC

#include "symbol.hpp"

enum IrKind {
    IR_FUNC,
    IR_GLOBAL_VAR,
    IR_GLOBAL_CONST,
    IR_STRUCT,      // uses ir_TypeDef
    IR_ALIAS,       // uses ir_TypeDef 
    IR_ENUM,        // uses ir_TypeDef

    IR_BLOCK,
    IR_IF,
    IR_WHILE,
    IR_DO_WHILE,    // uses ir_While
    IR_FOR,
    IR_MATCH,
    IR_UNSAFE,      // uses ir_Block
    IR_LOCAL_VAR,
    IR_LOCAL_CONST,
    IR_ASSIGN,
    IR_CPD_ASSIGN,
    IR_INCDEC,
    IR_EXPR_STMT,
    IR_RETURN,
    IR_BREAK,
    IR_CONTINUE,
    IR_FALLTHRU,

    IR_TEST_MATCH,
    IR_CAST,
    IR_BINOP,
    IR_UNOP,
    IR_ADDR,
    IR_DEREF,
    IR_CALL,
    IR_INDEX,
    IR_SLICE,
    IR_FIELD,
    IR_DEREF_FIELD, // uses ir_Field
    IR_NEW,
    IR_NEW_ARRAY,
    IR_NEW_STRUCT,
    IR_ARRAY_LIT,
    IR_STRUCT_LIT,
    IR_ENUM_LIT,
    IR_IDENT,
    IR_NUM_LIT,
    IR_FLOAT_LIT,
    IR_BOOL_LIT,
    IR_STRING_LIT,
    IR_NULL,

    IR_MACRO_SIZEOF,
    IR_MACRO_ALIGNOF,
    IR_MACRO_FUNCADDR,

    IRS_COUNT
};

struct IrNode {
    IrKind kind;
    TextSpan span;
};

/* -------------------------------------------------------------------------- */

enum IrOpKind {
    IROP_ADD,
    IROP_SUB,
    IROP_MUL,
    IROP_DIV,
    IROP_MOD,
    IROP_SHL,
    IROP_SHR,
    IROP_EQ,
    IROP_NE,
    IROP_LT,
    IROP_GT,
    IROP_LE,
    IROP_GE,
    IROP_BWAND,
    IROP_BWOR,
    IROP_BWXOR,
    IROP_LGAND,
    IROP_LGOR,

    IROP_NEG,
    IROP_NOT,
    IROP_BWNEG
};

enum IrAllocMode {
    IRMEM_STACK,
    IRMEM_HEAP,
    IRMEM_GLOBAL
};

struct IrFieldInit {
    IrExpr* expr;
    size_t field_index;
};

struct IrExpr : public IrNode {
    Type* type;
    bool assignable;

    union {
        struct {
            IrExpr* expr;
            std::span<IrExpr*> patterns;
        } ir_TestMatch;
        struct {
            IrExpr* expr;
        } ir_Cast;
        struct {
            IrExpr* lhs;
            IrExpr* rhs;
            IrOpKind op;
        } ir_Binop;
        struct {
            IrExpr* expr;
            IrOpKind op;
        } ir_Unop;
        struct {
            IrExpr* expr;
        } ir_Addr;
        struct {
            IrExpr* expr;
        } ir_Deref;
        struct {
            IrExpr* func;
            std::span<IrExpr*> args;
        } ir_Call;
        struct {
            IrExpr* expr;
            IrExpr* index;
        } ir_Index;
        struct {
            IrExpr* expr;
            IrExpr* low;
            IrExpr* hi;
        } ir_Slice;
        struct {
            IrExpr* expr;
            size_t field_index;
        } ir_Field;
        struct {
            IrAllocMode alloc_mode;
        } ir_New;
        struct {
            IrAllocMode alloc_mode;
            IrExpr* len;
        } ir_NewArray;
        struct {
            std::span<IrFieldInit> field_inits;
            IrAllocMode alloc_mode;
        } ir_NewStruct;
        struct {
            std::span<IrExpr*> items;
            IrAllocMode alloc_mode;
        } ir_ArrayLit;
        struct {
            std::span<IrFieldInit> field_inits;
        } ir_StructLit;
        struct {
            size_t tag_value;
        } ir_EnumLit;
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
        struct {
            IrExpr* arg;
        } ir_ValueMacro;
    };
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
        struct {
            Type* enum_type;
            size_t variant_id;
        } v_enum;
    };
};

/* -------------------------------------------------------------------------- */

struct IrStmt;

struct IrIfBranch {
    IrExpr* cond;
    IrStmt* body;
};

struct IrCaseBlock {
    std::span<IrExpr*> patterns;
    IrStmt* body;
};

struct IrStmt : public IrNode {
    union {
        struct {
            std::span<IrStmt*> stmts;
        } ir_Block;
        struct {
            std::span<IrIfBranch> branches;
            IrStmt* else_stmt;
        } ir_If;
        struct {
            IrExpr* cond;
            IrStmt* body;
            IrStmt* else_stmt;
        } ir_While;
        struct {
            IrStmt* iter_var;
            IrExpr* cond;
            IrStmt* update_stmt;
            IrStmt* body;
            IrStmt* else_stmt;
        } ir_For;
        struct {
            IrExpr* expr;
            std::span<IrCaseBlock> cases;
            bool is_implicit_exhaustive;
        } ir_Match;
        struct {
            Symbol* symbol;
            IrExpr* init;
        } ir_LocalVar;
        struct {
            Symbol* symbol;
            ConstValue* init;
        } ir_LocalConst;
        struct {
            IrExpr* lhs;
            IrExpr* rhs;
        } ir_Assign;
        struct {
            IrExpr* lhs;
            IrExpr* rhs;
            IrOpKind op;
        } ir_CpdAssign;
        struct {
            IrExpr* expr;
            IrOpKind op;
        } ir_IncDec;
        struct {
            IrExpr* expr;
        } ir_ExprStmt;
        struct {
            IrExpr* expr;
        } ir_Return;
    };
};

/* -------------------------------------------------------------------------- */

struct IrDecl : public IrNode {
    std::span<Attribute> attrs;

    struct {
        Symbol* symbol;
        std::span<Symbol*> params;
        Type* return_type;
        IrStmt* body;
    } ir_Func;
    struct {
        Symbol* symbol;
        IrExpr* init;
    } ir_GlobalVar;
    struct {
        Symbol* symbol;
        ConstValue* init;
    } ir_GlobalConst;
    struct {
        Symbol* symbol;
    } ir_TypeDef;
};

#endif
#ifndef AST_H_INC
#define AST_H_INC

#include "symbol.hpp"

// AstOpKind enumerates the different possible opcodes for AST operators.
enum AstOpKind {
    AOP_ADD,
    AOP_SUB,
    AOP_MUL,
    AOP_DIV,
    AOP_MOD,

    AOP_SHL,
    AOP_SHR,

    AOP_EQ,
    AOP_NE,
    AOP_LT,
    AOP_GT,
    AOP_LE,
    AOP_GE,

    AOP_BWAND,
    AOP_BWOR,
    AOP_BWXOR,
    AOP_BWNEG,

    AOP_LGAND,
    AOP_LGOR,

    AOP_NEG,
    AOP_NOT,

    AOP_NONE
};

// AstAllocMode indicates what kind of allocation should be generated for an
// "allocated" object.  In most cases, this defaults to heap allocation.
// However, the compiler will always try to choose the most efficient allocation
// mode it can.  
enum AstAllocMode {
    A_ALLOC_STACK,
    A_ALLOC_GLOBAL,
    A_ALLOC_HEAP
};

// MetadataTag represents a Berry metadata tag.
struct MetadataTag {
    // The name of the tag.
    std::string_view name;

    // The source span containing the tag name.
    TextSpan name_span;

    // The value of the tag (may be empty if no value).
    std::string_view value;

    // The source span containing the value (if it exists).
    TextSpan value_span;
};

typedef std::unordered_map<std::string_view, MetadataTag> MetadataMap;

/* -------------------------------------------------------------------------- */

enum AstKind {
    AST_FUNC,
    AST_GLOBAL_VAR,
    AST_STRUCT_DEF,

    AST_BLOCK,
    AST_IF,
    AST_WHILE,
    AST_FOR,
    AST_LOCAL_VAR,
    AST_ASSIGN,
    AST_INCDEC,
    AST_EXPR_STMT,
    AST_RETURN,
    AST_BREAK,
    AST_CONTINUE,

    AST_CAST,
    AST_BINOP,
    AST_UNOP,
    AST_ADDR,
    AST_DEREF,
    AST_CALL,
    AST_INDEX,
    AST_SLICE,
    AST_FIELD,
    AST_ARRAY,
    AST_NEW,
    AST_IDENT,
    AST_INT,
    AST_FLOAT,
    AST_BOOL,
    AST_NULL,
    AST_STR,

    ASTS_COUNT
};

struct AstNode {
    AstKind kind;
    TextSpan span;
};

/* -------------------------------------------------------------------------- */

struct AstExpr : public AstNode {
    Type* type;
    bool immut;

    union {
        struct {
            AstExpr* src;
        } an_Cast;
        struct {
            AstExpr* lhs;
            AstExpr* rhs;
            AstOpKind op;
        } an_Binop;
        struct {
            AstExpr* operand;
            AstOpKind op;
        } an_Unop;
        struct {
            AstExpr* elem;
        } an_Addr;
        struct {
            AstExpr* ptr;
        } an_Deref;
        struct {
            AstExpr* array;
            AstExpr* index;
        } an_Index;
        struct {
            AstExpr* array;
            AstExpr* start_index;
            AstExpr* end_index;
        } an_Slice;
        struct {
            AstExpr* root;
            std::string_view field_name;

            // This field is only used for static_get
            Symbol* imported_sym;
        } an_Field;
        struct {
            AstExpr* func;
            std::span<AstExpr*> args;
        } an_Call;

        struct {
            std::span<AstExpr*> elems;
            AstAllocMode alloc_mode;
        } an_Array;
        struct {
            Type* elem_type;
            AstExpr* size_expr;
            AstAllocMode alloc_mode;
        } an_New;

        struct {
            std::string_view temp_name;

            union {
                Symbol* symbol;
                size_t dep_id;
            };
        } an_Ident;
        struct {
            uint64_t value;
        } an_Int;
        struct {
            double value;
        } an_Float;
        struct {
            bool value;
        } an_Bool;
        struct {
            std::string_view value;
        } an_String;
    };

    bool IsLValue() const;
};

/* -------------------------------------------------------------------------- */

struct AstStmt;

struct AstCondBranch {
    TextSpan span;
    AstExpr* cond_expr;
    AstStmt* body;
};

struct AstStmt : public AstNode {
    union {
        struct {
            std::span<AstStmt*> stmts;
        } an_Block;
        struct {
            std::span<AstCondBranch> branches;
            AstStmt* else_block;
        } an_If;
        struct {
            AstExpr* cond_expr;
            AstStmt* body;
            AstStmt* else_block;
            bool is_do_while;
        } an_While;
        struct {
            AstStmt* var_def;
            AstExpr* cond_expr;
            AstStmt* update_stmt;
            AstStmt* body;
            AstStmt* else_block;
        } an_For;

        struct {
            Symbol* symbol;
            AstExpr* init;
        } an_LocalVar;
        struct {
            AstExpr* lhs;
            AstExpr* rhs;
            AstOpKind assign_op;
        } an_Assign;
        struct {
            AstExpr* lhs;
            AstOpKind op;
        } an_IncDec;
        struct {
            AstExpr* expr;
        } an_ExprStmt;

        struct {
            AstExpr* value;
        } an_Return;
    };
};

/* -------------------------------------------------------------------------- */

struct StructFieldAttr {
    std::span<MetadataTag> metadata;
};

struct AstDef : public AstNode {
    std::span<MetadataTag> metadata;

    union {
        struct {
            Symbol* symbol;
            std::span<Symbol*> params;
            Type* return_type;
            AstStmt* body;
        } an_Func;
        struct {
            Symbol* symbol;
            AstExpr* init;
        } an_GlobalVar;
        struct {
            Symbol* symbol;
            std::span<StructFieldAttr> field_attrs;
        } an_StructDef;
    };
};


#endif
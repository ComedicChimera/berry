#ifndef AST_H_INC
#define AST_H_INC

#include "symbol.hpp"

enum AstKind {
    AST_FUNC,
    AST_GLVAR,
    AST_STRUCT,
    AST_ALIAS,
    AST_ENUM,

    AST_BLOCK,
    AST_IF,
    AST_WHILE,
    AST_FOR,
    AST_MATCH,
    AST_UNSAFE,
    AST_LOCAL_VAR,
    AST_ASSIGN,
    AST_INCDEC,
    AST_EXPR_STMT,
    AST_RETURN,
    AST_BREAK,
    AST_CONTINUE,
    AST_FALLTHROUGH,

    AST_TEST_MATCH,
    AST_CAST,
    AST_BINOP,
    AST_UNOP,
    AST_ADDR,
    AST_DEREF,
    AST_CALL,
    AST_INDEX,
    AST_SLICE,
    AST_FIELD,
    AST_STATIC_GET,
    AST_ARRAY,
    AST_NEW,
    AST_STRUCT_LIT_POS,
    AST_STRUCT_LIT_NAMED,
    AST_STRUCT_LIT_TYPE,
    AST_STRUCT_PTR_LIT_POS,
    AST_STRUCT_PTR_LIT_NAMED,
    AST_ENUM_LIT,
    AST_ENUM_LIT_TYPE,
    AST_IDENT,
    AST_INT,
    AST_FLOAT,
    AST_BOOL,
    AST_NULL,
    AST_STRING,

    AST_SIZEOF,
    AST_ALIGNOF,

    AST_PATTERN_LIST,

    ASTS_COUNT
};

struct AstNode {
    AstKind kind;
    TextSpan span;
};

/* -------------------------------------------------------------------------- */

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

struct AstExpr;

struct AstNamedFieldInit {
    AstExpr* ident;
    AstExpr* expr;
    size_t field_index;
};

struct AstExpr : public AstNode {
    Type* type;
    bool immut;

    union {
        struct {
            AstExpr* expr;
            AstExpr* pattern;
        } an_TestMatch;
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
            union {
                size_t field_index;
                Symbol* imported_sym;
            };
        } an_Field;
        struct {
            AstExpr* func;
            std::span<AstExpr*> args;
            AstAllocMode alloc_mode;
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
            AstExpr* root;
            std::span<AstExpr*> field_inits;
            AstAllocMode alloc_mode;
        } an_StructLitPos;
        struct {
            AstExpr* root;
            std::span<AstNamedFieldInit> field_inits;
            AstAllocMode alloc_mode;
        } an_StructLitNamed;

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

        struct {
            Type* type_arg;
        } an_TypeMacro;

        struct {
            std::span<AstExpr*> patterns;
        } an_PatternList;
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

struct ConstValue;

#define CONST_VALUE_MARKER ((ConstValue*)1)

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
            AstExpr* expr;
            std::span<AstCondBranch> cases;
            bool is_enum_exhaustive;
        } an_Match;

        struct {
            Symbol* symbol;
            AstExpr* init_expr;
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

// Attribute represents a Berry definition attribute.
struct Attribute {
    // The name of the tag.
    std::string_view name;

    // The source span containing the tag name.
    TextSpan name_span;

    // The value of the tag (may be empty if no value).
    std::string_view value;

    // The source span containing the value (if it exists).
    TextSpan value_span;
};
typedef std::unordered_map<std::string_view, Attribute> AttributeMap;

struct AstVariantInit {
    AstExpr* init_expr;
    ConstValue* value;

    AstVariantInit(AstExpr* init_expr) 
    : init_expr(init_expr), value(nullptr) {}
};

struct AstDef : public AstNode {
    size_t parent_file_number;
    std::span<Attribute> attrs;

    union {
        struct {
            Symbol* symbol;
            std::span<Symbol*> params;
            Type* return_type;
            AstStmt* body;
        } an_Func;
        struct {
            Symbol* symbol;
            AstExpr* init_expr;
            ConstValue* const_value;
        } an_GlVar;
        struct {
            Symbol* symbol;
        } an_Struct;
        struct {
            Symbol* symbol;
        } an_Alias;
        struct {
            Symbol* symbol;
            std::span<AstVariantInit> variant_inits;
        } an_Enum;
    };
};

#endif
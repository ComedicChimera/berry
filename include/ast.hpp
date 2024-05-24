#ifndef AST_H_INC
#define AST_H_INC

#include "symbol.hpp"
#include "token.hpp"

enum AstKind {
    AST_FUNC,
    AST_VAR,
    AST_CONST,      // uses an_Var
    AST_TYPEDEF,
    AST_METHOD,
    AST_FACTORY,

    AST_BLOCK,
    AST_IF,
    AST_WHILE,
    AST_DO_WHILE,   // uses an_While
    AST_FOR,
    AST_MATCH,
    AST_UNSAFE,     // uses an_Block

    AST_ASSIGN,
    AST_INCDEC,
    AST_RETURN,
    AST_BREAK,
    AST_CONTINUE,
    AST_FALLTHRU,

    AST_TEST_MATCH,
    AST_CAST,
    AST_BINOP,
    AST_UNOP,
    AST_ADDR,
    AST_DEREF,
    AST_CALL,
    AST_INDEX,
    AST_SLICE,
    AST_SELECTOR,
    AST_NEW,
    AST_NEW_ARRAY,
    AST_NEW_STRUCT,     // uses an_StructLit
    AST_ARRAY_LIT,      // uses an_ExprList
    AST_STRUCT_LIT,    
    AST_UNSAFE_EXPR, 
    AST_IDENT,
    AST_NUM_LIT,
    AST_FLOAT_LIT,
    AST_BOOL_LIT,
    AST_RUNE_LIT,
    AST_STRING_LIT,
    AST_NULL,

    AST_MACRO_SIZEOF,           // uses an_Macro
    AST_MACRO_ALIGNOF,          // uses an_Macro
    AST_MACRO_ATOMIC_CAS_WEAK,  // uses an_Macro
    AST_MACRO_ATOMIC_LOAD,      // uses an_Macro
    AST_MACRO_ATOMIC_STORE,     // uses an_Macro

    AST_TYPE_PRIM,
    AST_TYPE_ARRAY,
    AST_TYPE_SLICE,
    AST_TYPE_FUNC,
    AST_TYPE_STRUCT,
    AST_TYPE_ENUM,

    AST_EXPR_LIST,
    AST_NAMED_INIT,
    AST_DOT,

    ASTS_COUNT
};

struct AstNode;

struct AstFuncParam {
    TextSpan span;
    std::string_view name;
    AstNode* type;
};

struct AstStructField {
    TextSpan span;
    std::string_view name;
    AstNode* type;
    bool exported;
};

struct AstCondBranch {
    TextSpan span;
    AstNode* cond;
    AstNode* body;
};

struct AstOper {
    TextSpan span;
    TokenKind tok_kind;
};

struct AstNode {
    AstKind kind;
    TextSpan span;

    union {
        struct {
            Symbol* symbol;
            AstNode* func_type;
            AstNode* body;
        } an_Func;
        struct {
            Symbol* symbol;
            AstNode* type;
            AstNode* init;
        } an_Var;
        struct {
            Symbol* symbol;
            AstNode* type;
        } an_TypeDef;
        struct {
            AstNode* bind_type;
            std::string_view name;
            TextSpan name_span;
            AstNode* func_type;
            AstNode* body;
            bool exported;
        } an_Method;
        struct {
            AstNode* bind_type;
            AstNode* func_type;
            AstNode* body;
            bool exported;
        } an_Factory;

        struct {
            std::span<AstNode*> stmts;
        } an_Block;
        struct {
            std::span<AstCondBranch> branches;
            AstNode* else_stmt;
        } an_If;
        struct {
            AstNode* cond;
            AstNode* body;
            AstNode* else_stmt;
        } an_While;
        struct {
            AstNode* iter_var;
            AstNode* cond;
            AstNode* update_stmt;
            AstNode* body;
            AstNode* else_stmt; 
        } an_For;
        struct {
            AstNode* expr;
            std::span<AstCondBranch> cases;
        } an_Match;

        struct {
            AstNode* lhs;
            AstNode* rhs;
            AstOper op;
        } an_Assign;
        struct {
            AstNode* lhs;
            AstOper op;
        } an_IncDec;
        struct {
            AstNode* expr;
        } an_Return;

        struct {
            AstNode* expr;
            AstNode* pattern;
        } an_TestMatch;
        struct {
            AstNode* expr;
            AstNode* dest_type;
        } an_Cast;
        struct {
            AstNode* lhs;
            AstNode* rhs;
            AstOper op;
        } an_Binop;
        struct {
            AstNode* expr;
            AstOper op;
        } an_Unop;
        struct {
            AstNode* expr;
        } an_Addr;
        struct {
            AstNode* expr;
        } an_Deref;
        struct {
            AstNode* func;
            std::span<AstNode*> args;
        } an_Call;
        struct {
            AstNode* expr;
            AstNode* index;
        } an_Index;
        struct {
            AstNode* expr;
            AstNode* start_index;
            AstNode* end_index;
        } an_Slice;
        struct {
            AstNode* expr;
            std::string_view field_name;
        } an_Sel;
        struct {
            AstNode* type;
        } an_New;
        struct {
            AstNode* type;
            AstNode* len;
        } an_NewArray;
        struct {
            AstNode* type;
            std::span<AstNode*> field_inits;
        } an_StructLit;
        struct {
            AstNode* expr;
        } an_UnsafeExpr;
        struct {
            std::string_view name;
            Symbol* symbol; // bound late
        } an_Ident;
        struct {
            uint64_t value;
        } an_Num;
        struct {
            double value;
        } an_Float;
        struct {
            bool value;
        } an_Bool;
        struct {
            int32_t value;
        } an_Rune;
        struct {
            std::string_view value;
        } an_String;
        
        struct {
            std::span<AstNode*> args;
        } an_Macro;

        struct {
            Type* prim_type;
        } an_TypePrim;
        struct {
            AstNode* elem_type;
            AstNode* len;
        } an_TypeArray;
        struct {
            AstNode* elem_type;
        } an_TypeSlice;
        struct {
            std::span<AstFuncParam> params;
            AstNode* return_type;
        } an_TypeFunc;
        struct {
            std::span<AstStructField> fields;
        } an_TypeStruct;
        struct {
            std::span<AstNode*> variants;
        } an_TypeEnum;

        struct {
            std::span<AstNode*> exprs;
        } an_ExprList;
        struct {
            std::string_view name;
            AstNode* init;
        } an_NamedInit;
    };
};

#endif
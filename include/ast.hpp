#ifndef AST_H_INC
#define AST_H_INC

#include "symbol.hpp"

struct Visitor;

// AstFlags enumerates the different values that can be returned from
// ast->GetFlags().  These are used to indicate AST nodes that have some special
// significance to visitors.
typedef int AstFlags;
enum {
    ASTF_NONE = 0,
    ASTF_EXPR = 1,
    ASTF_NULL = 2,
    ASTF_IDENT = 4
};

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

    AOP_LGAND,
    AOP_LGOR,

    AOP_NEG,
    AOP_NOT,

    AOP_NONE
};

// AstNode is a node in the AST.
struct AstNode {
    // span is the source span containing the node.
    TextSpan span;

    // always_returns indicates whether this node always returns.  That is to
    // say, when it is encountered in a block, no code after it will execute.
    bool always_returns;

    AstNode(const TextSpan& span)
    : span(span)
    , always_returns(false)
    {}

    // Accept passes this node to a visitor.
    virtual void Accept(Visitor* v) = 0;

    // GetFlags returns the AST nodes flags.
    inline virtual AstFlags GetFlags() const { return ASTF_NONE; }
};

// AstExpr is an expression AST node.
struct AstExpr : public AstNode {
    // type is the data type the expression evaluates to.
    Type* type;

    // llvm_value is the LLVM value returned by this expr.
    llvm::Value* llvm_value;

    // IsImmut indicates whether the expression can be assigned to.
    inline virtual bool IsImmut() const { return false; }

    // IsLValue indicates whether the expression is an l-value (has a
    // well-defined memory location).
    inline virtual bool IsLValue() const { return false; }

    AstExpr(const TextSpan& span, Type* type)
    : AstNode(span)
    , type(type)
    {}

    inline AstFlags GetFlags() const override { return ASTF_EXPR; }
};

// MetadataTag represents a Berry metadata tag.
struct MetadataTag {
    // The name of the tag.
    std::string name;

    // The source span containing the tag name.
    TextSpan name_span;

    // The value of the tag (may be empty if no value).
    std::string value;

    // The source span containing the value (if it exists).
    TextSpan value_span;
};

typedef std::unordered_map<std::string, MetadataTag> Metadata;

// AstDef is a definition AST node.
struct AstDef : public AstNode {
    // metadata contains the definition's metadata tags.
    Metadata metadata;

    AstDef(const TextSpan& span, Metadata&& metadata)
    : AstNode(span)
    , metadata(std::move(metadata))
    {}
};

/* -------------------------------------------------------------------------- */

// AstFuncDef represents a function definition.
struct AstFuncDef : public AstDef {
    // symbol is the symbol associated with the function definition.
    Symbol* symbol;

    // params is the list of parameter symbols.
    std::vector<Symbol*> params;

    // return_type is the return type of the function.
    Type* return_type;

    // body is the body of the AST.  This will be NULL if there is no body.
    std::unique_ptr<AstNode> body;

    AstFuncDef(
        const TextSpan& span, 
        Metadata&& meta, 
        Symbol* symbol, 
        std::vector<Symbol*>&& params, 
        Type* return_type, 
        std::unique_ptr<AstNode>&& body
    )
    : AstDef(span, std::move(meta))
    , symbol(symbol)
    , params(std::move(params))
    , return_type(return_type)
    , body(std::move(body))
    {}

    void Accept(Visitor* v) override;
};

// AstLocalVarDef represents a local variable definition.
struct AstLocalVarDef : public AstNode {
    // symbol is the associated variable symbol.
    Symbol* symbol;

    // init is the variable's initializer.  This will be NULL if the variable
    // has no initializer.
    std::unique_ptr<AstExpr> init;

    // array_size is the size of the array allocated by this local def. If no
    // array is allocated, then this will be 0.
    size_t array_size;

    AstLocalVarDef(
        const TextSpan& span, 
        Symbol* symbol, 
        std::unique_ptr<AstExpr>&& init, 
        size_t array_size
    )
    : AstNode(span)
    , symbol(symbol)
    , init(std::move(init))
    , array_size(array_size)
    {}

    void Accept(Visitor* v) override;
};

// AstGlobalVarDef represents a global variable definition.
struct AstGlobalVarDef : public AstDef {
    // var_def is the equivalent local variable definition.
    std::unique_ptr<AstLocalVarDef> var_def;

    AstGlobalVarDef(Metadata&& meta, std::unique_ptr<AstLocalVarDef>&& var_def)
    : AstDef(var_def->span, std::move(meta))
    , var_def(std::move(var_def))
    {}

    void Accept(Visitor* v) override;
};

/* -------------------------------------------------------------------------- */

// AstBlock represents a block of statements.
struct AstBlock : public AstNode {
    // stmts is the statements comprising the block.
    std::vector<std::unique_ptr<AstNode>> stmts;

    AstBlock(const TextSpan& span, std::vector<std::unique_ptr<AstNode>>&& stmts)
    : AstNode(span)
    , stmts(std::move(stmts))
    {}
 
    void Accept(Visitor* v) override;
};

// AstCondBranch represents a conditional branch of an if statement.
struct AstCondBranch : public AstNode {
    // cond_expr is the conditional expression of the branch.
    std::unique_ptr<AstExpr> cond_expr;

    // body is the body of the node (to be executed if cond_expr is true).
    std::unique_ptr<AstNode> body;

    AstCondBranch(
        const TextSpan& span, 
        std::unique_ptr<AstExpr>&& cond_expr,
         std::unique_ptr<AstNode>&& body
    ) 
    : AstNode(span)
    , cond_expr(std::move(cond_expr))
    , body(std::move(body))
    {}

    void Accept(Visitor* v) override;
};

// AstIfTree represents a if/elif/else tree.
struct AstIfTree : public AstNode {
    // branches stores all the conditional branches of the if statement.
    std::vector<AstCondBranch> branches;

    // else_body is the body of the else clause or nullptr if there isn't one.
    std::unique_ptr<AstNode> else_body;

    AstIfTree(std::vector<AstCondBranch>&& branches, std::unique_ptr<AstNode>&& else_body)
    : AstNode(SpanOver(branches[0].span, else_body ? else_body->span : branches.back().span))
    , branches(std::move(branches))
    , else_body(std::move(else_body))
    {}

    void Accept(Visitor* v) override;
};

// AstWhileLoop represents a while or do-while loop.
struct AstWhileLoop : public AstNode {
    // cond_expr is the conditional expression of the loop.
    std::unique_ptr<AstExpr> cond_expr;

    // body is the body of the loop.
    std::unique_ptr<AstNode> body;

    // else_clause stores the loop's else clause or nullptr if there isn't one.
    std::unique_ptr<AstNode> else_clause;

    // is_do_while indicates whether this is a while loop or a do-while loop.
    bool is_do_while;

    AstWhileLoop(
        const TextSpan& span,
        std::unique_ptr<AstExpr>&& cond_expr,
        std::unique_ptr<AstNode>&& body,
        std::unique_ptr<AstNode>&& else_clause,
        bool is_do_while
    )
    : AstNode(span)
    , cond_expr(std::move(cond_expr))
    , body(std::move(body))
    , else_clause(std::move(else_clause))
    , is_do_while(is_do_while)
    {}

    void Accept(Visitor* v) override;
};

// AstForLoop represents a tripartite (C-style) for loop.
struct AstForLoop : public AstNode {
    // var_def stores the loop's var_def or nullptr if there isn't one.
    std::unique_ptr<AstLocalVarDef> var_def;

    // cond_expr stores the loop's conditional expression or nullptr if there
    // isn't one.
    std::unique_ptr<AstExpr> cond_expr;

    // update_stmt stores the "update statement" of the loop (called at the end
    // of every loop iteration) or nullptr if there isn't one.
    std::unique_ptr<AstNode> update_stmt;

    // body stores the body of the for loop.
    std::unique_ptr<AstNode> body;

    // else_clause stores the loop's else clause or nullptr if there isn't one.
    std::unique_ptr<AstNode> else_clause;

    AstForLoop(
        const TextSpan& span,
        std::unique_ptr<AstLocalVarDef>&& var_def,
        std::unique_ptr<AstExpr>&& cond_expr,
        std::unique_ptr<AstNode>&& update_stmt,
        std::unique_ptr<AstNode>&& body,
        std::unique_ptr<AstNode>&& else_clause
    )
    : AstNode(span)
    , var_def(std::move(var_def))
    , cond_expr(std::move(cond_expr))
    , update_stmt(std::move(update_stmt))
    , body(std::move(body))
    , else_clause(std::move(else_clause))
    {}

    void Accept(Visitor* v) override;
};

/* -------------------------------------------------------------------------- */

// AstIncDec represents an increment or a decrement statement (ex: `a++`).
struct AstIncDec : public AstNode {
    // lhs is the expression being incremented.
    std::unique_ptr<AstExpr> lhs;

    // op_kind is the operator used (`+` for `++`, `-` for `--`).
    AstOpKind op_kind;

    AstIncDec(const TextSpan& span, std::unique_ptr<AstExpr>&& lhs, AstOpKind op)
    : AstNode(span)
    , lhs(std::move(lhs))
    , op_kind(op)
    {}

    void Accept(Visitor* v) override;
};

// AstAssign represents an assignment state (ex: `x = 2`).
struct AstAssign : public AstNode {
    // lhs the left hand side of the assignment.
    std::unique_ptr<AstExpr> lhs;

    // rhs is the right hand side of the assignment.
    std::unique_ptr<AstExpr> rhs;

    // assign_op_kind is the optional compound assignment operation (ex: `+`
    // for `+=`). If no compound op is used, then this will be AOP_NONE.
    AstOpKind assign_op_kind;

    AstAssign(
        const TextSpan& span, 
        std::unique_ptr<AstExpr>&& lhs, 
        std::unique_ptr<AstExpr>&& rhs, 
        AstOpKind assign_op = AOP_NONE
    )
    : AstNode(span)
    , lhs(std::move(lhs))
    , rhs(std::move(rhs))
    , assign_op_kind(assign_op)
    {}

    void Accept(Visitor* v) override;
};

// AstReturn represents a return statement (ex: `return a`).
struct AstReturn : public AstNode {
    // value is the returned value.  his may be null if nothing is returned.
    std::unique_ptr<AstExpr> value;

    AstReturn(const TextSpan& span)
    : AstNode(span)
    , value(nullptr)
    {}

    AstReturn(const TextSpan& span, std::unique_ptr<AstExpr>&& value) 
    : AstNode(span)
    , value(std::move(value))
    {
        always_returns = true;
    }

    void Accept(Visitor* v) override;
};

// AstBreak represents a break statement (ex: `break`).
struct AstBreak : public AstNode {
    AstBreak(const TextSpan& span) : AstNode(span) {}

    void Accept(Visitor* v) override;
};

// AstContinue represents a continue statement (ex: `continue`).
struct AstContinue : public AstNode {
    AstContinue(const TextSpan& span) : AstNode(span) {}

    void Accept(Visitor* v) override;
};

/* -------------------------------------------------------------------------- */

// AstCast represents a type cast (ex: `x as int`).  The evaluated type of the
// cast stores the destination type (cast->type == dest_type).
struct AstCast : public AstExpr {
    // src is the expression being type cast.
    std::unique_ptr<AstExpr> src;

    AstCast(const TextSpan& span, std::unique_ptr<AstExpr>&& src, Type* dest_type)
    : AstExpr(span, dest_type)
    , src(std::move(src))
    {}

    void Accept(Visitor* v) override;
};

// AstBinaryOp represents a binary operation (ex: `a + b`).
struct AstBinaryOp : public AstExpr {
    // op_kind is the operator applied.
    AstOpKind op_kind;

    // lhs and rhs are the left-hand and right-hand operands respectively.
    std::unique_ptr<AstExpr> lhs, rhs;

    AstBinaryOp(
        const TextSpan& span, 
        AstOpKind aop, 
        std::unique_ptr<AstExpr>&& lhs, 
        std::unique_ptr<AstExpr>&& rhs
    ) : AstExpr(span, nullptr)
    , op_kind(aop)
    , lhs(std::move(lhs))
    , rhs(std::move(rhs))
    {}

    void Accept(Visitor* v) override;
};

// AstUnaryOp represents a (non-pointer) unary operation (ex: `!a`, `-a`).
struct AstUnaryOp : public AstExpr {
    // op_kind is the operator applied.
    AstOpKind op_kind;

    // operand is the operand of the unary operator.
    std::unique_ptr<AstExpr> operand;

    AstUnaryOp(const TextSpan& span, AstOpKind aop, std::unique_ptr<AstExpr>&& operand)
    : AstExpr(span, nullptr)
    , op_kind(aop)
    , operand(std::move(operand))
    {}

    void Accept(Visitor* v) override;
};

// AstAddrOf represents an address-of/indirect operation (ex: `&a`, `&const a`).
struct AstAddrOf : public AstExpr {
    // elem is the element/value being referenced.
    std::unique_ptr<AstExpr> elem;

    // is_const indicates whether the reference is a const reference.
    bool is_const;

    AstAddrOf(const TextSpan& span, std::unique_ptr<AstExpr>&& elem, bool is_const = false)
    : AstExpr(span, nullptr)
    , elem(std::move(elem))
    , is_const(is_const)
    {}

    void Accept(Visitor* v) override;
};

// AstDeref represents a derference operation (ex: `*a`).
struct AstDeref : public AstExpr {
    // ptr is the pointer being dereferenced.
    std::unique_ptr<AstExpr> ptr;

    AstDeref(const TextSpan& span, std::unique_ptr<AstExpr>&& ptr) 
    : AstExpr(span, nullptr)
    , ptr(std::move(ptr))
    {}

    // *a is mutable if and only if a is.
    inline bool IsImmut() const override { return dynamic_cast<PointerType*>(ptr->type)->immut; }

    // *a is always an l-value (even if it is immutable).
    inline bool IsLValue() const override { return true; }

    void Accept(Visitor* v) override;
};

// AstIndex represents an index expression (ex: `arr[i]`).
struct AstIndex : public AstExpr {
    // array is the array being indexed.
    std::unique_ptr<AstExpr> array;

    // index is the index expression being used.
    std::unique_ptr<AstExpr> index;

    AstIndex(const TextSpan& span, std::unique_ptr<AstExpr>&& array, std::unique_ptr<AstExpr>&& index)
    : AstExpr(span, nullptr)
    , array(std::move(array))
    , index(std::move(index))
    {}

    void Accept(Visitor* v) override;
};

// AstSlice represents a slice expression (ex: `arr[i:j]`, `arr[:k]`).
struct AstSlice : public AstExpr {
    // array is the array being sliced.
    std::unique_ptr<AstExpr> array;

    // start_index is the start index expression or nullptr if there isn't one.
    std::unique_ptr<AstExpr> start_index;

    // end_index is the end index expression or nullptr if there isn't one.
    std::unique_ptr<AstExpr> end_index;

    AstSlice(
        const TextSpan& span, 
        std::unique_ptr<AstExpr>&& array,
        std::unique_ptr<AstExpr>&& start_index,
        std::unique_ptr<AstExpr> end_index
    ) 
    : AstExpr(span, nullptr)
    , array(std::move(array))
    , start_index(std::move(start_index))
    , end_index(std::move(end_index))
    {}

    void Accept(Visitor* v) override;
};

// AstFieldAccess represents a field or method access (ex: `s.x`).
struct AstFieldAccess : public AstExpr {
    // root is the root expression of the field access.
    std::unique_ptr<AstExpr> root;

    // field_name is the name of the accessed field.
    std::string field_name;

    AstFieldAccess(const TextSpan& span, std::unique_ptr<AstExpr>&& root, std::string&& field_name)
    : AstExpr(span, nullptr)
    , root(std::move(root))
    , field_name(std::move(field_name))
    {}

    void Accept(Visitor* v) override;
};

// AstCall represents a function call (ex: `fn(a, b)`).
struct AstCall : public AstExpr {
    // func is the function being called.
    std::unique_ptr<AstExpr> func;

    // args is the argument expressions to the call.
    std::vector<std::unique_ptr<AstExpr>> args;

    AstCall(const TextSpan& span, std::unique_ptr<AstExpr>&& func, std::vector<std::unique_ptr<AstExpr>>&& args)
    : AstExpr(span, nullptr)
    , func(std::move(func))
    , args(std::move(args))
    {}

    void Accept(Visitor* v) override;
};

/* -------------------------------------------------------------------------- */

// AstArrayLit represents an array literal.
struct AstArrayLit : public AstExpr {
    // elements is the elements of the array.
    std::vector<std::unique_ptr<AstExpr>> elements;

    AstArrayLit(const TextSpan& span, std::vector<std::unique_ptr<AstExpr>>&& elems)
    : AstExpr(span, nullptr)
    , elements(std::move(elems))
    {}

    void Accept(Visitor* v) override;
};

/* -------------------------------------------------------------------------- */

// AstIdent represents an identifier (ex: `a`, `my_func`).
struct AstIdent : public AstExpr {
    // temp_name contains the name of the identifier until its corresponding
    // symbol is found.  This field should only be accessed during
    // initialization and symbol resolution.  It will be cleared once a symbol
    // is located to save memory.
    std::string temp_name;

    // symbol is the symbol associated with the identifier.  This will be NULL
    // until symbol resolution occurs.
    Symbol* symbol;

    // GetName returns the name associated with the identifier.
    inline std::string_view GetName() const { return symbol == NULL ? temp_name : symbol->name; }

    AstIdent(const TextSpan& span, std::string&& temp_name)
    : AstExpr(span, nullptr)
    , temp_name(std::move(temp_name))
    , symbol(nullptr)
    {}

    void Accept(Visitor* v) override;

    inline AstFlags GetFlags() const override { return ASTF_EXPR | ASTF_IDENT; }
    
    inline bool IsImmut() const override { return symbol->immut; }
    inline bool IsLValue() const override { return true; }
};

// AstIntLit represents an integer literal (ex: `12`, `0xff`, `'a'`).  Note that
// rune literals are represented as i32 literals in the AST.
struct AstIntLit : public AstExpr {
    // value is the value of the integer literal.
    uint64_t value;

    AstIntLit(const TextSpan& span, uint64_t value)
    : AstExpr(span, nullptr)
    , value(value)
    {}

    AstIntLit(const TextSpan& span, Type* type, uint64_t value)
    : AstExpr(span, type)
    , value(value)
    {}
 
    void Accept(Visitor* v) override;
};

// AstFloatLit represents a floating-point literal (ex: `12.25`, `1e-9`). 
struct AstFloatLit : public AstExpr {
    // value is the value of the float literal.
    double value;

    AstFloatLit(const TextSpan& span, double value)
    : AstExpr(span, nullptr)
    , value(value)
    {}

    void Accept(Visitor* v) override;
};

// AstBoolLit represents a boolean literal (ex: `true`, `false`).
struct AstBoolLit : public AstExpr {
    // value is the value of the boolean literal.
    bool value;

    AstBoolLit(const TextSpan& span, bool value)
    : AstExpr(span, &prim_bool_type)
    , value(value)
    {}
    
    void Accept(Visitor* v) override;
};

// AstNullLit represents a null literal (ex: `null`).
struct AstNullLit : public AstExpr {
    AstNullLit(const TextSpan& span) : AstExpr(span, nullptr) {}

    void Accept(Visitor* v) override;

    inline AstFlags GetFlags() const override { return ASTF_EXPR | ASTF_NULL; }
};

// AstStringLit represents a string literal (ex: `"hello"`).
struct AstStringLit : public AstExpr {
    // value is the string value of the literal.
    std::string value;

    AstStringLit(const TextSpan& span, std::string&& value)
    : AstExpr(span, &prim_string_type)
    , value(std::move(value))
    {}

    void Accept(Visitor* v) override;
};

#endif
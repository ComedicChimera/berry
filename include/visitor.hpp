#ifndef VISITOR_H_INC
#define VISITOR_H_INC

#include "ast.hpp"

// Visitor is the interface for all AST visitors.
struct Visitor {
    virtual void Visit(AstFuncDef& node) = 0;
    virtual void Visit(AstLocalVarDef& node) = 0;
    virtual void Visit(AstGlobalVarDef& node) = 0;
    virtual void Visit(AstBlock& node) = 0;
    virtual void Visit(AstCast& node) = 0;
    virtual void Visit(AstBinaryOp& node) = 0;
    virtual void Visit(AstUnaryOp& node) = 0;
    virtual void Visit(AstAddrOf& node) = 0;
    virtual void Visit(AstDeref& node) = 0;
    virtual void Visit(AstCall& node) = 0;
    virtual void Visit(AstIdent& node) = 0;
    virtual void Visit(AstIntLit& node) = 0;
    virtual void Visit(AstFloatLit& node) = 0;
    virtual void Visit(AstBoolLit& node) = 0;
    virtual void Visit(AstNullLit& node) = 0;
    virtual void Visit(AstCondBranch& node) = 0;
    virtual void Visit(AstIfTree& node) = 0;
    virtual void Visit(AstWhileLoop& node) = 0;
    virtual void Visit(AstForLoop& node) = 0;
    virtual void Visit(AstIncDec& node) = 0;
    virtual void Visit(AstAssign& node) = 0;
    virtual void Visit(AstReturn& node) = 0;
    virtual void Visit(AstBreak& node) = 0;
    virtual void Visit(AstContinue& node) = 0;
    virtual void Visit(AstIndex& node) = 0;
    virtual void Visit(AstSlice& node) = 0;
    virtual void Visit(AstArrayLit& node) = 0;
    virtual void Visit(AstStringLit& node) = 0;
    virtual void Visit(AstFieldAccess& node) = 0;

protected:
    // NOTE: Is there a better way to do this?  (C++ is scuffed af).
    virtual void visitNode(std::unique_ptr<AstNode>& node);
    virtual void visitNode(std::unique_ptr<AstExpr>& node);
    virtual void visitNode(std::unique_ptr<AstDef>& node);
};

#endif
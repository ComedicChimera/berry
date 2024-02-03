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

protected:
    // NOTE: Is there a better way to do this?  (C++ is scuffed af).
    inline void visitNode(std::unique_ptr<AstNode>& node) { node->Accept(this); }
    inline void visitNode(std::unique_ptr<AstExpr>& node) { node->Accept(this); }
};

#endif
#ifndef AST_PRINT_H_INC
#define AST_PRINT_H_INC

#include "visitor.hpp"

// AstPrinter is a debugging/utility visitor that prints an AST to standard out.
struct AstPrinter : public Visitor {
    void Visit(AstFuncDef& node) override;
    void Visit(AstLocalVarDef& node) override;
    void Visit(AstGlobalVarDef& node) override;
    void Visit(AstBlock& node) override;
    void Visit(AstCast& node) override;
    void Visit(AstBinaryOp& node) override;
    void Visit(AstUnaryOp& node) override;
    void Visit(AstAddrOf& node) override;
    void Visit(AstDeref& node) override;
    void Visit(AstCall& node) override;
    void Visit(AstIdent& node) override;
    void Visit(AstIntLit& node) override;
    void Visit(AstFloatLit& node) override;
    void Visit(AstBoolLit& node) override;
    void Visit(AstNullLit& node) override;
    void Visit(AstCondBranch& node) override;
    void Visit(AstIfTree& node) override;
    void Visit(AstWhileLoop& node) override;
    void Visit(AstForLoop& node) override;
    void Visit(AstIncDec& node) override;
    void Visit(AstAssign& node) override;
    void Visit(AstReturn& node) override;
    void Visit(AstBreak& node) override;
    void Visit(AstContinue& node) override;
    void Visit(AstIndex& node) override;
    void Visit(AstSlice& node) override;
    void Visit(AstArrayLit& node) override;
    void Visit(AstStringLit& node) override;
    void Visit(AstFieldAccess& node) override;

private:
    void visitOrEmpty(std::unique_ptr<AstNode> &node);
    void visitOrEmpty(std::unique_ptr<AstExpr>& node);
};

#endif
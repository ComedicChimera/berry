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
};

#endif
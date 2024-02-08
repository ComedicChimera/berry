#include "visitor.hpp"

void AstFuncDef::Accept(Visitor* v) { v->Visit(*this); }
void AstLocalVarDef::Accept(Visitor* v) { v->Visit(*this); }
void AstGlobalVarDef::Accept(Visitor* v) { v->Visit(*this); }
void AstBlock::Accept(Visitor* v) { v->Visit(*this); }
void AstCast::Accept(Visitor* v) { v->Visit(*this); }
void AstBinaryOp::Accept(Visitor* v) { v->Visit(*this); }
void AstUnaryOp::Accept(Visitor* v) { v->Visit(*this); }
void AstAddrOf::Accept(Visitor* v) { v->Visit(*this); }
void AstDeref::Accept(Visitor* v) { v->Visit(*this); }
void AstCall::Accept(Visitor* v) { v->Visit(*this); }
void AstIdent::Accept(Visitor* v) { v->Visit(*this); }
void AstIntLit::Accept(Visitor* v) { v->Visit(*this); }
void AstFloatLit::Accept(Visitor* v) { v->Visit(*this); }
void AstBoolLit::Accept(Visitor* v) { v->Visit(*this); }
void AstNullLit::Accept(Visitor* v) { v->Visit(*this); }

/* -------------------------------------------------------------------------- */

void Visitor::visitNode(std::unique_ptr<AstNode>& node) { node->Accept(this); }
void Visitor::visitNode(std::unique_ptr<AstExpr>& node) { node->Accept(this); }
void Visitor::visitNode(std::unique_ptr<AstDef>& node) { node->Accept(this); }
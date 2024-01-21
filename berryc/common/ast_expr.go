package common

import "berryc/dtypes"

type AstCast struct {
	AstExprBase

	SrcExpr AstExpr
}

type AstOpKind uint8

const (
	AOP_ADD AstOpKind = iota
	AOP_SUB
	AOP_MUL
	AOP_DIV
	AOP_MOD

	AOP_BAND
	AOP_BOR
	AOP_BXOR

	AOP_NEG
)

type AstBinaryOp struct {
	AstExprBase

	OpKind   AstOpKind
	Lhs, Rhs AstExpr
}

type AstUnaryOp struct {
	AstExprBase

	OpKind  AstOpKind
	Operand AstExpr
}

type AstAddrOf struct {
	AstExprBase

	Elem     AstExpr
	ConstRef bool
}

type AstDeref struct {
	AstExprBase

	Ptr AstExpr
}

func (d *AstDeref) IsMutable() bool {
	return !d.Ptr.GetType().(*dtypes.PointerType).Const
}

type AstCall struct {
	AstExprBase

	Func AstExpr
	Args []AstExpr
}

type AstIdent struct {
	AstExprBase

	Name   string
	Symbol *Symbol
}

func (id *AstIdent) IsMutable() bool {
	return id.Symbol.Mutable
}

type AstIntLit struct {
	AstExprBase

	Value uint64
}

type AstFloatLit struct {
	AstExprBase

	Value float64
}

type AstBoolLit struct {
	AstExprBase

	Value bool
}

type AstNullLit struct {
	AstExprBase
}

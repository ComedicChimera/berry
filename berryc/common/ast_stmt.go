package common

type AstBlock struct {
	AstBase

	Stmts []AstNode
}

type AstLocalVar struct {
	AstBase

	Symbol      *Symbol
	Initializer AstExpr
}

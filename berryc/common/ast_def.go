package common

import (
	"berryc/dtypes"
)

type AstFuncDef struct {
	AstDefBase

	Symbol     *Symbol
	Params     []*Symbol
	ReturnType dtypes.Type
	Body       AstNode
}

/* -------------------------------------------------------------------------- */

type AstGlobalVar struct {
	AstDefBase

	VarDecl *AstLocalVar
}

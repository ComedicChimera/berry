package walk

import (
	"berryc/common"
)

func (w *Walker) walkFuncDef(fd *common.AstFuncDef) {
	w.pushScope()

	for _, param := range fd.Params {
		w.declareLocal(param)
	}

	if fd.Body != nil {
		if block, ok := fd.Body.(*common.AstBlock); ok {
			w.walkBlock(block)
		}

		// TODO: expr bodies
	}

	w.popScope()
}

func (w *Walker) walkGlobalVar(gv *common.AstGlobalVar) {
	if gv.VarDecl.Initializer != nil {
		w.walkExpr(gv.VarDecl.Initializer)

		w.mustSubType(
			gv.VarDecl.Symbol.Type,
			gv.VarDecl.Initializer.GetType(),
			gv.VarDecl.Initializer.GetSpan(),
		)

		w.inferUntypeds()
	}
}

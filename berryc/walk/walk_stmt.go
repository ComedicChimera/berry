package walk

import "berryc/common"

func (w *Walker) walkStmt(stmt common.AstNode) {
	switch v := stmt.(type) {
	case *common.AstLocalVar:
		w.walkLocalVar(v)
	case common.AstExpr:
		w.walkExpr(v)
		w.inferUntypeds()
	}
}

func (w *Walker) walkLocalVar(localVar *common.AstLocalVar) {
	if localVar.Initializer != nil {
		w.walkExpr(localVar.Initializer)
	}

	if localVar.Symbol.Type == nil {
		w.inferUntypeds()

		localVar.Symbol.Type = localVar.Initializer.GetType().Inner()
	} else {
		w.mustSubType(
			localVar.Symbol.Type,
			localVar.Initializer.GetType(),
			localVar.Initializer.GetSpan(),
		)

		w.inferUntypeds()
	}

	w.declareLocal(localVar.Symbol)
}

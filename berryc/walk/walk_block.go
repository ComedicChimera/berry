package walk

import "berryc/common"

func (w *Walker) walkBlock(block *common.AstBlock) {
	w.pushScope()

	for _, stmt := range block.Stmts {
		w.walkStmt(stmt)
	}

	w.popScope()
}

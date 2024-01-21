package walk

import (
	"berryc/common"
	"berryc/dtypes"
	"berryc/report"
	"berryc/util"
	"fmt"
)

type Scope map[string]*common.Symbol

type untypedEntry struct {
	utype *dtypes.Untyped
	span  *report.TextSpan
}

type ExprContext struct {
	utable *dtypes.UntypedTable
	utypes []untypedEntry
}

func (ec *ExprContext) Reset() {
	ec.utable.Reset()
	ec.utypes = nil
}

type Walker struct {
	srcFile *common.SourceFile

	localScopes []Scope
	exprCtx     *ExprContext
}

/* -------------------------------------------------------------------------- */

func NewWalker(srcFile *common.SourceFile) *Walker {
	return &Walker{
		srcFile: srcFile,
		exprCtx: &ExprContext{
			utable: dtypes.NewUntypedTable(),
		},
	}
}

func (w *Walker) WalkDef(def common.AstDef) {
	defer w.cleanup()
	defer report.Catch()

	w.validateMetadata(def.GetMetadata())

	switch v := def.(type) {
	case *common.AstFuncDef:
		w.walkFuncDef(v)
	case *common.AstGlobalVar:
		w.walkGlobalVar(v)
	}
}

/* -------------------------------------------------------------------------- */

func (w *Walker) lookup(name string, span *report.TextSpan) *common.Symbol {
	for i := len(w.localScopes) - 1; i >= 0; i-- {
		if sym, ok := w.localScopes[i][name]; ok {
			return sym
		}
	}

	if sym, ok := w.srcFile.Parent.SymbolTable[name]; ok {
		return sym
	}

	w.error(span, "undefined symbol: '%s'", name)
	return nil
}

func (w *Walker) declareLocal(sym *common.Symbol) {
	scope := w.localScopes[len(w.localScopes)-1]
	if _, ok := scope[sym.Name]; ok {
		w.error(sym.Span, "multiple symbols with name '%s' defined in same scope", sym.Name)
	}

	scope[sym.Name] = sym
}

func (w *Walker) pushScope() {
	w.localScopes = append(w.localScopes, make(Scope))
}

func (w *Walker) popScope() {
	w.localScopes = w.localScopes[:len(w.localScopes)-1]
}

/* -------------------------------------------------------------------------- */

func (w *Walker) mustEqual(a, b dtypes.Type, span *report.TextSpan) {
	if !dtypes.MustEqual(a, b) {
		w.error(span, "type mismatch: %s v. %s", util.DumpString(a), util.DumpString(b))
	}
}

func (w *Walker) mustSubType(super, sub dtypes.Type, span *report.TextSpan) {
	if !dtypes.MustSubtype(super, sub) {
		w.error(span, "%s cannot be implicitly converted to %s", util.DumpString(sub), util.DumpString(super))
	}
}

func (w *Walker) currExprContext() *ExprContext {
	return w.exprCtx
}

func (w *Walker) inferUntypeds() {
	ctx := w.currExprContext()
	ctx.utable.DefaultAll()

	for _, ute := range ctx.utypes {
		if !ute.utype.IsConcrete() {
			w.error(ute.span, "unable to infer type of '%s'", util.DumpString(ute.utype))
		} else {
			ute.utype.MakeConcrete(ute.utype.Inner())
		}
	}

	w.exprCtx.Reset()
}

func (w *Walker) newUntyped(kind dtypes.UntypedKind, span *report.TextSpan) *dtypes.Untyped {
	ctx := w.currExprContext()

	ut := ctx.utable.NewUntyped(kind)
	ctx.utypes = append(ctx.utypes, untypedEntry{
		utype: ut,
		span:  span,
	})

	return ut
}

/* -------------------------------------------------------------------------- */

func (w *Walker) cleanup() {
	w.exprCtx.Reset()

	w.localScopes = nil
}

func (w *Walker) error(span *report.TextSpan, format string, a ...any) {
	report.Throw(&report.SourceError{
		Message: fmt.Sprintf(format, a...),
		Info: &report.SourceInfo{
			ModName:     w.srcFile.Parent.Name,
			DisplayPath: w.srcFile.DisplayPath,
			Span:        span,
		},
	})
}

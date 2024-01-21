package walk

import (
	"berryc/common"
	"berryc/dtypes"
	"berryc/util"
)

func (w *Walker) walkExpr(expr common.AstExpr) {
	switch v := expr.(type) {
	case *common.AstBinaryOp:
		w.walkExpr(v.Lhs)
		w.walkExpr(v.Rhs)

		v.Type = w.checkIntrinsicBinaryOp(v.OpKind, v.Lhs, v.Rhs, v.Span)
	case *common.AstUnaryOp:
		w.walkExpr(v.Operand)

		v.Type = w.checkIntrinsicUnaryOp(v.OpKind, v.Operand)
	case *common.AstDeref:
		{
			w.walkExpr(v.Ptr)

			if ptrType, ok := v.Ptr.GetType().Inner().(*dtypes.PointerType); ok {
				v.Type = ptrType.ElemType
			} else {
				w.error(v.Ptr.GetSpan(), "expected a pointer")
			}
		}
	case *common.AstAddrOf:
		w.walkExpr(v.Elem)
		v.Type = &dtypes.PointerType{ElemType: v.Elem.GetType()}
	case *common.AstCall:
		w.walkCall(v)
	case *common.AstCast:
		w.walkExpr(v.SrcExpr)

		if !dtypes.MustCast(v.Type, v.SrcExpr.GetType()) {
			w.error(
				v.SrcExpr.GetSpan(),
				"cannot cast '%s' to '%s'",
				util.DumpString(v.SrcExpr.GetType()),
				util.DumpString(v.Type),
			)
		}
	case *common.AstIdent:
		v.Symbol = w.lookup(v.Name, v.Span)
		v.Type = v.Symbol.Type
	case *common.AstIntLit:
		if v.Type == nil {
			v.Type = w.newUntyped(dtypes.UK_NUMBER, v.Span)
		}
	case *common.AstFloatLit:
		if v.Type == nil {
			v.Type = w.newUntyped(dtypes.UK_FLOAT, v.Span)
		}
	case *common.AstNullLit:
		v.Type = w.newUntyped(dtypes.UK_NULL, v.Span)
	}
}

func (w *Walker) walkCall(call *common.AstCall) {
	w.walkExpr(call.Func)

	for _, arg := range call.Args {
		w.walkExpr(arg)
	}

	if ft, ok := call.Func.GetType().Inner().(*dtypes.FuncType); ok {
		if len(call.Args) != len(ft.Params) {
			w.error(call.Span, "expected %d parameters, received %d", len(ft.Params), len(call.Args))
		}

		for i, arg := range call.Args {
			w.mustSubType(ft.Params[i], arg.GetType(), arg.GetSpan())
		}

		call.Type = ft.ReturnType
	}
}

package walk

import (
	"berryc/common"
	"berryc/dtypes"
	"berryc/report"
	"berryc/util"
)

var opKindToString = map[common.AstOpKind]string{
	common.AOP_ADD:  "+",
	common.AOP_SUB:  "-",
	common.AOP_MUL:  "*",
	common.AOP_DIV:  "/",
	common.AOP_MOD:  "%",
	common.AOP_BAND: "&",
	common.AOP_BOR:  "|",
	common.AOP_BXOR: "^",
	common.AOP_NEG:  "-",
}

func (w *Walker) checkIntrinsicBinaryOp(op common.AstOpKind, lhs, rhs common.AstExpr, span *report.TextSpan) dtypes.Type {
	switch op {
	case common.AOP_ADD, common.AOP_SUB, common.AOP_MUL, common.AOP_DIV, common.AOP_MOD:
		w.mustEqual(lhs.GetType(), rhs.GetType(), span)

		// No need to check RHS here since LHS == RHS
		if dtypes.MustNumberType(lhs.GetType()) {
			return lhs.GetType()
		}
	case common.AOP_BAND, common.AOP_BOR, common.AOP_BXOR:
		w.mustEqual(lhs.GetType(), rhs.GetType(), span)

		// Ditto for RHS here
		if dtypes.MustIntType(lhs.GetType()) {
			return lhs.GetType()
		}
	}

	w.error(
		span,
		"cannot apply '%s' to %s and %s",
		opKindToString[op],
		util.DumpString(lhs.GetType()),
		util.DumpString(rhs.GetType()),
	)
	return nil
}

func (w *Walker) checkIntrinsicUnaryOp(op common.AstOpKind, operand common.AstExpr) dtypes.Type {
	switch op {
	case common.AOP_NEG:
		if dtypes.MustNumberType(operand.GetType()) {
			return operand.GetType()
		}
	}

	w.error(
		operand.GetSpan(),
		"cannot apply '%s' to type %s",
		opKindToString[op],
		util.DumpString(operand.GetType()),
	)
	return nil
}

#include "checker.hpp"

std::unordered_map<HirOpKind, std::string> hir_op_kind_to_name {
    { HIROP_ADD, "+" },
    { HIROP_SUB, "-" },
    { HIROP_MUL, "*" },
    { HIROP_DIV, "/" },
    { HIROP_MOD, "%" },
    { HIROP_SHL, "<<" },
    { HIROP_SHR, ">>" },
    { HIROP_EQ, "==" },
    { HIROP_NE, "!=" },
    { HIROP_LT, "<" },
    { HIROP_GT, ">" },
    { HIROP_LE, "<=" },
    { HIROP_GE, ">=" },
    { HIROP_BWAND, "&" },
    { HIROP_BWOR, "|" },
    { HIROP_BWXOR, "^" },
    { HIROP_LGAND, "&&" },
    { HIROP_LGOR, "||" },
    { HIROP_NEG, "-" },
    { HIROP_NOT, "!" },
    { HIROP_BWNEG, "~" }
};


Type* Checker::mustApplyBinaryOp(const TextSpan& span, HirOpKind op, Type* lhs_type, Type* rhs_type) {
    tctx.flags |= TC_INFER;

    auto* lhs_outer_type = lhs_type;
    auto* rhs_outer_type = rhs_type;
    lhs_type = lhs_type->Inner();
    rhs_type = rhs_type->Inner();

    Type* return_type { nullptr };
    switch (op) {
    case HIROP_SUB:
        if (unsafe_depth > 0 && lhs_type->kind == TYPE_PTR && rhs_type->kind == TYPE_PTR && tctx.Equal(lhs_type, rhs_type)) {
            return_type = platform_int_type;
        }
        // Fallthrough
    case HIROP_ADD:
        if (return_type == nullptr && unsafe_depth > 0) {
            return_type = maybeApplyPtrArithOp(lhs_type, rhs_type);
        }
        
        if (return_type == nullptr && tctx.Equal(lhs_type, rhs_type) && tctx.IsNumberType(lhs_type)) {
            return_type = lhs_type;
        }
        break;
    case HIROP_MUL:
    case HIROP_DIV:
    case HIROP_MOD:
        if (tctx.Equal(lhs_type, rhs_type) && tctx.IsNumberType(lhs_type)) {
            return_type = lhs_type;
        }
        break;
    case HIROP_SHL:
    case HIROP_SHR:
        if (unsafe_depth > 0 && lhs_type->kind == TYPE_PTR && tctx.IsIntType(rhs_type)) {
            return_type = lhs_type;
        }

        if (return_type == nullptr && tctx.Equal(lhs_type, rhs_type) && tctx.IsIntType(lhs_type)) {
            return_type = lhs_type;
        }
        break;
    case HIROP_BWAND:
    case HIROP_BWOR:
    case HIROP_BWXOR:
        if (unsafe_depth > 0) {
            return_type = maybeApplyPtrArithOp(lhs_type, rhs_type);
        }

        if (return_type == nullptr && tctx.Equal(lhs_type, rhs_type) && tctx.IsIntType(lhs_type)) {
            return_type = lhs_type;
        }
        break;
    case HIROP_EQ:
    case HIROP_NE:
        lhs_type = lhs_type->Inner();

        switch (lhs_type->kind) {
        case TYPE_SLICE:
        case TYPE_FUNC:
        case TYPE_STRUCT:
            break;
        case TYPE_NAMED:
            if (lhs_type->FullUnwrap()->kind == TYPE_STRUCT)
                break;
            // fallthrough
        default:
            if (maybeApplyPtrCompareOp(lhs_type, rhs_type) || tctx.Equal(lhs_type, rhs_type)) {
                return_type = &prim_bool_type;
            }
        }      
        break;
    case HIROP_LT:
    case HIROP_GT:
    case HIROP_LE:
    case HIROP_GE:
        if (maybeApplyPtrCompareOp(lhs_type, rhs_type)) {
            return_type = &prim_bool_type;
        } else if (tctx.Equal(lhs_type, rhs_type) && tctx.IsNumberType(lhs_type)) {
            return_type = &prim_bool_type;
        }
        break;
    case HIROP_LGAND:
    case HIROP_LGOR:
        if (tctx.Equal(lhs_type, &prim_bool_type) && tctx.Equal(rhs_type, &prim_bool_type)) {
            return_type = &prim_bool_type;
        }
        break;
    default:
        Panic("unsupported binary ast operator in checker: {}", (int)op);
        break;
    }

    if (return_type == nullptr) {
        Assert(hir_op_kind_to_name.contains(op), "missing op string for operator");

        fatal(span, "cannot apply {} operator to {} and {}", hir_op_kind_to_name[op], lhs_outer_type->ToString(), rhs_outer_type->ToString());
    }

    tctx.flags ^= TC_INFER;
    return return_type;
}

Type* Checker::maybeApplyPtrArithOp(Type* lhs_type, Type* rhs_type) {
    if (lhs_type->kind == TYPE_PTR) {
        if (tctx.IsIntType(rhs_type)) {
            return lhs_type;
        }
    } else if (rhs_type->kind == TYPE_PTR) {
        if (tctx.IsIntType(lhs_type)) {
            return rhs_type;
        }
    }

    return nullptr;
}

Type* Checker::maybeApplyPtrCompareOp(Type* lhs_type, Type* rhs_type) {
    if (lhs_type->kind == TYPE_PTR) {
        if (tctx.IsNullType(rhs_type)) {
            tctx.Equal(lhs_type, rhs_type);
            return lhs_type;
        } else if (rhs_type->kind == TYPE_PTR) {
            if (tctx.Equal(lhs_type, rhs_type)) {
                return lhs_type;
            }
        } else if (tctx.IsIntType(rhs_type)) {
            return lhs_type;
        }
    } else if (rhs_type->kind == TYPE_PTR) {
        if (tctx.IsNullType(lhs_type)) {
            tctx.Equal(lhs_type, rhs_type);
            return rhs_type;
        } else if (tctx.IsIntType(lhs_type)) {
            return rhs_type;
        }
    }

    return nullptr;
}

/* -------------------------------------------------------------------------- */

Type* Checker::mustApplyUnaryOp(const TextSpan& span, HirOpKind op, Type* operand_type) {
    tctx.flags |= TC_INFER;

    Type* return_type { nullptr };
    switch (op) {
    case HIROP_NOT:
        if (tctx.Equal(operand_type, &prim_bool_type)) {
            return_type = &prim_bool_type;
        }
        break;
    case HIROP_NEG:
        if (tctx.IsNumberType(operand_type)) {
            return_type = operand_type;
        }
        break;
    case HIROP_BWNEG:
        if (tctx.IsIntType(operand_type)) {
            return_type = operand_type;
        }
        break;
    default:
        Panic("unsupported unary ast operator in checker: {}", (int)op);
        break;
    }

    if (return_type == nullptr) {
        Assert(hir_op_kind_to_name.find(op) != hir_op_kind_to_name.end(), "missing op string for operator");

        fatal(span, "cannot apply {} operator to {}", hir_op_kind_to_name[op], operand_type->ToString());
    }

    tctx.flags ^= TC_INFER;
    return return_type;
}
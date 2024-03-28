#include "checker.hpp"

std::unordered_map<AstOpKind, std::string> ast_op_kind_to_name {
    { AOP_ADD, "+" },
    { AOP_SUB, "-" },
    { AOP_MUL, "*" },
    { AOP_DIV, "/" },
    { AOP_MOD, "%" },
    { AOP_SHL, "<<" },
    { AOP_SHR, ">>" },
    { AOP_EQ, "==" },
    { AOP_NE, "!=" },
    { AOP_LT, "<" },
    { AOP_GT, ">" },
    { AOP_LE, "<=" },
    { AOP_GE, ">=" },
    { AOP_BWAND, "&" },
    { AOP_BWOR, "|" },
    { AOP_BWXOR, "^" },
    { AOP_LGAND, "&&" },
    { AOP_LGOR, "||" },
    { AOP_NEG, "-" },
    { AOP_NOT, "!" },
    { AOP_BWNEG, "~" }
};


Type* Checker::mustApplyBinaryOp(const TextSpan& span, AstOpKind aop, Type* lhs_type, Type* rhs_type) {
    tctx.flags |= TC_INFER;

    Type* return_type { nullptr };
    switch (aop) {
    case AOP_ADD:
    case AOP_SUB:
        return_type = maybeApplyPtrArithOp(lhs_type, rhs_type);
        
        if (return_type == nullptr && tctx.Equal(lhs_type, rhs_type) && tctx.IsNumberType(lhs_type)) {
            return_type = lhs_type;
        }
        break;
    case AOP_MUL:
    case AOP_DIV:
    case AOP_MOD:
        if (tctx.Equal(lhs_type, rhs_type) && tctx.IsNumberType(lhs_type)) {
            return_type = lhs_type;
        }
        break;
    case AOP_SHL:
    case AOP_SHR:
    case AOP_BWAND:
    case AOP_BWOR:
    case AOP_BWXOR:
        return_type = maybeApplyPtrArithOp(lhs_type, rhs_type);

        if (return_type == nullptr && tctx.Equal(lhs_type, rhs_type) && tctx.IsIntType(lhs_type)) {
            return_type = lhs_type;
        }
        break;
    case AOP_EQ:
    case AOP_NE:
        if (tctx.Equal(lhs_type, rhs_type)) {
            return_type = &prim_bool_type;
        }
        break;
    case AOP_LT:
    case AOP_GT:
    case AOP_LE:
    case AOP_GE:
        return_type = maybeApplyPtrArithOp(lhs_type, rhs_type);

        if (return_type == nullptr && tctx.Equal(lhs_type, rhs_type) && tctx.IsNumberType(lhs_type)) {
            return_type = &prim_bool_type;
        }
        break;
    case AOP_LGAND:
    case AOP_LGOR:
        if (tctx.Equal(lhs_type, &prim_bool_type) && tctx.Equal(rhs_type, &prim_bool_type)) {
            return_type = &prim_bool_type;
        }
        break;
    default:
        Panic("unsupported binary ast operator in checker: {}", (int)aop);
        break;
    }

    if (return_type == nullptr) {
        Assert(ast_op_kind_to_name.find(aop) != ast_op_kind_to_name.end(), "missing aop string for operator");

        fatal(span, "cannot apply {} operator to {} and {}", ast_op_kind_to_name[aop], lhs_type->ToString(), rhs_type->ToString());
    }

    tctx.flags ^= TC_INFER;
    return return_type;
}

Type* Checker::maybeApplyPtrArithOp(Type* lhs_type, Type* rhs_type) {
    lhs_type = lhs_type->Inner();
    rhs_type = rhs_type->Inner();

    if (lhs_type->kind == TYPE_PTR) {
        if (rhs_type->kind == TYPE_PTR) {
            if (tctx.Equal(lhs_type, rhs_type))
                return lhs_type;

            return nullptr;
        } else if (tctx.IsIntType(rhs_type)) {
            return lhs_type;
        }

        return lhs_type;
    } else if (rhs_type->kind == TYPE_PTR && tctx.IsIntType(lhs_type)) {
        return rhs_type;
    }

    return nullptr;
}

/* -------------------------------------------------------------------------- */

Type* Checker::mustApplyUnaryOp(const TextSpan& span, AstOpKind aop, Type* operand_type) {
    tctx.flags |= TC_INFER;

    Type* return_type { nullptr };
    switch (aop) {
    case AOP_NOT:
        if (tctx.Equal(operand_type, &prim_bool_type)) {
            return_type = &prim_bool_type;
        }
        break;
    case AOP_NEG:
        if (tctx.IsNumberType(operand_type)) {
            return_type = operand_type;
        }
        break;
    case AOP_BWNEG:
        if (tctx.IsIntType(operand_type)) {
            return_type = operand_type;
        }
        break;
    default:
        Panic("unsupported unary ast operator in checker: {}", (int)aop);
        break;
    }

    if (return_type == nullptr) {
        Assert(ast_op_kind_to_name.find(aop) != ast_op_kind_to_name.end(), "missing aop string for operator");

        fatal(span, "cannot apply {} operator to {}", ast_op_kind_to_name[aop], operand_type->ToString());
    }

    tctx.flags ^= TC_INFER;
    return return_type;
}
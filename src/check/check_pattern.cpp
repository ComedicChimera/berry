#include "checker.hpp"

std::pair<std::span<HirExpr*>, bool> Checker::checkCasePattern(AstNode* node, Type* expect_type) {
    if (node->kind == AST_EXPR_LIST) {
        std::vector<HirExpr*> hpatterns;
        for (auto* apattern : node->an_ExprList.exprs) {
            auto [hpattern, captures] = checkPattern(apattern, expect_type);
            if (captures) {
                fatal(node->span, "case with alternated patterns can't capture values");
            }

            hpatterns.push_back(hpattern);
        }

        return { arena.MoveVec(std::move(hpatterns)), false };
    }

    auto [hpattern, captures] = checkPattern(node, expect_type);
    return { arena.MoveVec<HirExpr*>({ hpattern }), captures };
}

std::pair<HirExpr*, bool> Checker::checkPattern(AstNode* node, Type* expect_type) {
    HirExpr* hexpr;
    switch (node->kind) {
    case AST_NUM_LIT:
        if (!tctx.IsNumberType(expect_type)) {
            error(node->span, "untyped number cannot match type {}", expect_type->ToString());
        }

        hexpr = allocExpr(HIR_NUM_LIT, node->span);
        hexpr->type = expect_type;
        hexpr->ir_Num.value = node->an_Num.value;
        break;
    case AST_FLOAT_LIT:
        if (expect_type->Inner()->kind != TYPE_FLOAT) {
            error(node->span, "untyped float cannot match type {}", expect_type->ToString());
        }

        hexpr = allocExpr(HIR_FLOAT_LIT, node->span);
        hexpr->type = expect_type;
        hexpr->ir_Float.value = node->an_Float.value;
        break;
    case AST_RUNE_LIT: 
        // Only one copy of prim_i32_type should ever be used so this is fine :)
        if (expect_type->Inner() != &prim_i32_type) {
            error(node->span, "rune cannot match type {}", expect_type->ToString());
        }

        hexpr = allocExpr(HIR_NUM_LIT, node->span);
        hexpr->type = &prim_i32_type;
        hexpr->ir_Num.value = (uint64_t)node->an_Rune.value;
        break;
    case AST_STRING_LIT:
        if (expect_type->Inner()->kind != TYPE_STRING) {
            error(node->span, "string cannot match type {}", expect_type->ToString());
        }

        hexpr = allocExpr(HIR_STRING_LIT, node->span);
        hexpr->type = &prim_string_type;
        hexpr->ir_String.value = node->an_String.value;
        break;
    case AST_BOOL_LIT:
        if (expect_type->Inner()->kind != TYPE_BOOL) {
            error(node->span, "bool cannot match type {}", expect_type->ToString());
        }

        hexpr = allocExpr(HIR_BOOL_LIT, node->span);
        hexpr->type = &prim_bool_type;
        hexpr->ir_Bool.value = node->an_Bool.value;
        break;
    case AST_IDENT: {
        hexpr = allocExpr(HIR_IDENT, node->span);
        hexpr->type = expect_type;

        if (node->an_Ident.name == "_") {
            hexpr->ir_Ident.symbol = nullptr;
        } else {
            hexpr->ir_Ident.symbol = arena.New<Symbol>(
                mod.id,
                node->an_Ident.name,
                node->span,
                SYM_VAR,
                0,
                expect_type,
                false
            );

            return { hexpr, true };
        }
    } break;
    case AST_SELECTOR: {
        auto* aroot = node->an_Sel.expr;
        Type* type;
        if (aroot->kind == AST_DOT) {
            type = expect_type;
        } else {
            type = checkTypeLabel(node->an_Sel.expr, true);
            if (!tctx.Equal(type, expect_type)) {
                error(node->span, "type {} cannot match {}", type->ToString(), expect_type->ToString());
            }
        }

        hexpr = checkEnumLit(node, type);
        getPatternCtx().enum_usages.insert(hexpr->ir_EnumLit.tag_value);
    } break;
    default:
        Panic("unimplemented pattern in checking: {}", (int)node->kind);
        break;
    }

    return { hexpr, false };
}

/* -------------------------------------------------------------------------- */

void Checker::declarePatternCaptures(HirExpr* pattern) {
    switch (pattern->kind) {
    case HIR_IDENT:
        if (pattern->ir_Ident.symbol)
            declareLocal(pattern->ir_Ident.symbol);
        
        break;
    }
}

/* -------------------------------------------------------------------------- */

bool Checker::isEnumExhaustive(Type* expr_type) {
    expr_type = expr_type->FullUnwrap();

    if (expr_type->kind == TYPE_ENUM) {
        for (size_t variant_tag : expr_type->ty_Enum.tag_map) {
            if (!getPatternCtx().enum_usages.contains(variant_tag))
                return false;
        }

        return true;
    }

    return false;
}

/* -------------------------------------------------------------------------- */

Checker::PatternContext& Checker::getPatternCtx() {
    return pattern_ctx_stack.back();
}

void Checker::pushPatternCtx() {
    pattern_ctx_stack.emplace_back();
}

void Checker::popPatternCtx() {
    pattern_ctx_stack.pop_back();
}
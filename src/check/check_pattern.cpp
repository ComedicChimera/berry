#include "checker.hpp"

bool Checker::checkCasePattern(AstExpr* node, Type* expect_type, std::unordered_set<size_t>* enum_usages) {
    if (node->kind == AST_PATTERN_LIST) {
        for (auto* pattern : node->an_PatternList.patterns) {
            if (checkPattern(pattern, expect_type, enum_usages)) {
                error(node->span, "case with alternated patterns can't capture values");
            }
        }

        return false;
    }

    return checkPattern(node, expect_type, enum_usages);
}

bool Checker::checkPattern(AstExpr* node, Type* expect_type, std::unordered_set<size_t>* enum_usages) {
    switch (node->kind) {
    case AST_FLOAT:
    case AST_INT:
        if (node->type) {
            mustEqual(node->span, node->type, expect_type);
        } else {
            node->type = expect_type;
        }
        break;
    case AST_STR:
    case AST_BOOL:
        mustEqual(node->span, node->type, expect_type);
        break;
    case AST_ENUM_LIT: 
        if (node->an_Field.root->type == nullptr) {
            node->an_Field.root->type = expect_type;
        }

        checkEnumLit(node);

        if (enum_usages)
            enum_usages->insert(node->an_Field.field_index);

        mustEqual(node->span, node->type, expect_type); 
        break;
    case AST_IDENT: {
        node->type = expect_type;

        if (node->an_Ident.temp_name == "_")
            return false;

        auto* symbol = arena.New<Symbol>(
            mod.id,
            node->an_Ident.temp_name,
            node->span,
            SYM_VAR,
            0,
            expect_type,
            false
        );

        node->an_Ident.symbol = symbol;
        return true;
    } break;
    }

    return false;
}

/* -------------------------------------------------------------------------- */

void Checker::declarePatternCaptures(AstExpr* pattern) {
    switch (pattern->kind) {
    case AST_IDENT:
        if (pattern->an_Ident.symbol)
            declareLocal(pattern->an_Ident.symbol);
        
        break;
    }
}

/* -------------------------------------------------------------------------- */

bool Checker::isEnumExhaustive(Type* expr_type, const std::unordered_set<size_t>& enum_usages) {
    expr_type = expr_type->FullUnwrap();

    if (expr_type->kind == TYPE_ENUM) {
        for (size_t variant_id : expr_type->ty_Enum.name_map) {
            if (!enum_usages.contains(variant_id))
                return false;
        }

        return true;
    }

    return false;
}
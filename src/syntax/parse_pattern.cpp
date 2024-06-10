#include "parser.hpp"

AstNode* Parser::parseCasePattern() {
    std::vector<AstNode*> patterns;

    while (true) {
        patterns.push_back(parsePattern());

        if (has(TOK_PIPE)) {
            next();
        } else {
            break;
        }
    }

    if (patterns.size() == 1) {
        return patterns[0];
    } else {
        auto* alist = allocNode(AST_EXPR_LIST, SpanOver(patterns[0]->span, patterns.back()->span));
        alist->an_ExprList.exprs = ast_arena.MoveVec(std::move(patterns));
        return alist;
    }
}

AstNode* Parser::parsePattern() {
    switch (tok.kind) {
    case TOK_INTLIT: {
        next();

        uint64_t value = 0;
        if (!ConvertUint(prev.value, &value)) {
            error(prev.span, "integer literal is too big to be represented by any integer type");
        }

        auto* anum = allocNode(AST_NUM_LIT, prev.span);
        anum->an_Num.value = value;
        return anum;
    } break;
    case TOK_FLOATLIT: {
        next();

        double value = 0;
        try {
            value = std::stod(prev.value);
        } catch (std::out_of_range&) {
            error(prev.span, "float literal cannot be accurately represented by any float type");
        }

        auto* afloat = allocNode(AST_FLOAT_LIT, prev.span);
        afloat->an_Float.value = value;
        return afloat;
    } break;
    case TOK_RUNELIT: {
        next();

        auto* arune = allocNode(AST_RUNE_LIT, prev.span);
        arune->an_Rune.value = ConvertRuneLit(prev.value);
        return arune;
    } break;
    case TOK_BOOLLIT: {
        next();

        auto* abool = allocNode(AST_BOOL_LIT, prev.span);
        abool->an_Bool.value = prev.value == "true";
        return abool;
    } break;
    case TOK_STRLIT: {
        next();

        auto* astr = allocNode(AST_STRING_LIT, prev.span);
        astr->an_String.value = global_arena.MoveStr(std::move(prev.value));
        return astr;
    } break;
    case TOK_IDENT: {
        next();

        auto* ident = allocNode(AST_IDENT, prev.span);
        ident->an_Ident.name = ast_arena.MoveStr(std::move(prev.value));

        if (has(TOK_DOT)) {
            next();

            auto var_name_tok = wantAndGet(TOK_IDENT);

            auto* asel = allocNode(AST_SELECTOR, SpanOver(ident->span, var_name_tok.span));
            asel->an_Sel.expr = ident;
            asel->an_Sel.field_name = ast_arena.MoveStr(std::move(var_name_tok.value));
            return asel;
        }

        return ident;
    } break;
    case TOK_DOT: {
        next();

        auto* adot = allocNode(AST_DOT, prev.span);

        auto var_name_tok = wantAndGet(TOK_IDENT);

        auto* asel = allocNode(AST_SELECTOR, SpanOver(adot->span, var_name_tok.span));
        asel->an_Sel.expr = adot;
        asel->an_Sel.field_name = ast_arena.MoveStr(std::move(var_name_tok.value));
        return asel;
    } break;
    }

    reject("expected a pattern");
    return nullptr;
}
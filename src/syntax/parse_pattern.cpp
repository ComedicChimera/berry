#include "parser.hpp"

AstExpr* Parser::parseCasePattern() {
    std::vector<AstExpr*> patterns;

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
        auto* aplist = allocExpr(AST_PATTERN_LIST, SpanOver(patterns[0]->span, patterns.back()->span));
        aplist->an_PatternList.patterns = arena.MoveVec(std::move(patterns));
        return aplist;
    }
}

AstExpr* Parser::parsePattern() {
    switch (tok.kind) {
    case TOK_INTLIT: {
        next();

        uint64_t value = 0;
        if (!ConvertUint(prev.value, &value)) {
            error(prev.span, "integer literal is too big to be represented by any integer type");
        }

        auto* aint = allocExpr(AST_INT, prev.span);
        aint->an_Int.value = value;
        return aint;
    } break;
    case TOK_FLOATLIT: {
        next();

        double value = 0;
        try {
            value = std::stod(prev.value);
        } catch (std::out_of_range&) {
            error(prev.span, "float literal cannot be accurately represented by any float type");
        }

        auto* afloat = allocExpr(AST_FLOAT, prev.span);
        afloat->an_Float.value = value;
        return afloat;
    } break;
    case TOK_RUNELIT: {
        next();

        auto* aint = allocExpr(AST_INT, prev.span);
        aint->type = &prim_i32_type;
        aint->an_Int.value = ConvertRuneLit(prev.value);
        return aint;
    } break;
    case TOK_BOOLLIT: {
        next();

        auto* abool = allocExpr(AST_BOOL, prev.span);
        abool->type = &prim_bool_type;
        abool->an_Bool.value = prev.value == "true";
        return abool;
    } break;
    case TOK_STRLIT: {
        next();

        auto* astr = allocExpr(AST_STRING, prev.span);
        astr->type = &prim_string_type;
        astr->an_String.value = arena.MoveStr(std::move(prev.value));
        return astr;
    } break;
    case TOK_IDENT: {
        next();

        auto* ident = allocExpr(AST_IDENT, prev.span);
        ident->an_Ident.temp_name = arena.MoveStr(std::move(prev.value));
        ident->an_Ident.symbol = nullptr;

        if (has(TOK_DOT)) {
            next();

            auto var_name_tok = wantAndGet(TOK_IDENT);

            auto* aenum = allocExpr(AST_ENUM_LIT, SpanOver(ident->span, var_name_tok.span));
            aenum->an_Field.root = ident;
            aenum->an_Field.field_name = arena.MoveStr(std::move(var_name_tok.value));
            return aenum;
        }

        return ident;
    } break;
    case TOK_DOT: {
        next();

        auto* lit_type = allocExpr(AST_ENUM_LIT_TYPE, prev.span);

        auto var_name_tok = wantAndGet(TOK_IDENT);

        auto* aenum = allocExpr(AST_ENUM_LIT, SpanOver(lit_type->span, var_name_tok.span));
        aenum->an_Field.root = lit_type;
        aenum->an_Field.field_name = arena.MoveStr(std::move(var_name_tok.value));
        return aenum;
    } break;
    }

    reject("expected a pattern");
    return nullptr;
}
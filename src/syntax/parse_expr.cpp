#include "parser.hpp"

AstExpr* Parser::parseExpr() {
    auto* expr = parseBinaryOp(0);

    if (has(TOK_AS)) {
        next();

        auto dest_type = parseTypeLabel();

        auto* acast = allocExpr(AST_CAST, SpanOver(expr->span, prev.span));
        acast->type = dest_type;
        acast->an_Cast.src = expr;
        expr = acast;
    }

    if (has(TOK_MATCH)) {
        next();

        auto* pattern = parseCasePattern();

        auto* amatch = allocExpr(AST_TEST_MATCH, SpanOver(expr->span, prev.span));
        amatch->an_TestMatch.expr = expr;
        amatch->an_TestMatch.pattern = pattern;
        expr = amatch;
    }

    return expr;
}

/* -------------------------------------------------------------------------- */

std::unordered_map<TokenKind, AstOpKind> tok_to_aop {
    { TOK_PLUS, AOP_ADD },
    { TOK_MINUS, AOP_SUB },
    { TOK_STAR, AOP_MUL },
    { TOK_FSLASH, AOP_DIV },
    { TOK_MOD, AOP_MOD },
    { TOK_SHL, AOP_SHL },
    { TOK_SHR, AOP_SHR },
    { TOK_AMP, AOP_BWAND },
    { TOK_PIPE, AOP_BWOR },
    { TOK_CARRET, AOP_BWXOR },
    { TOK_EQ, AOP_EQ },
    { TOK_NE, AOP_NE },
    { TOK_LT, AOP_LT },
    { TOK_GT, AOP_GT },
    { TOK_LE, AOP_LE },
    { TOK_GE, AOP_GE },
    { TOK_AND, AOP_LGAND },
    { TOK_OR, AOP_LGOR },
};

#define PRED_TABLE_SIZE 8
std::vector<TokenKind> pred_table[PRED_TABLE_SIZE] = {
    { TOK_AND, TOK_OR },
    { TOK_EQ, TOK_NE, TOK_LT, TOK_GT, TOK_LE, TOK_GE },
    { TOK_PIPE },
    { TOK_CARRET },
    { TOK_AMP },
    { TOK_SHL, TOK_SHR },
    { TOK_PLUS, TOK_MINUS },
    { TOK_STAR, TOK_FSLASH, TOK_MOD }
};

AstExpr* Parser::parseBinaryOp(int pred_level) {
    if (pred_level >= PRED_TABLE_SIZE) {
        return parseUnaryOp();
    }

    auto lhs = parseBinaryOp(pred_level + 1);
    auto& pred_row = pred_table[pred_level];
    while (true) {
        auto it = std::find(pred_row.begin(), pred_row.end(), tok.kind);
        if (it == pred_row.end()) {
            break;
        }

        next();
        auto rhs = parseBinaryOp(pred_level + 1);

        auto lhs_span = lhs->span;
        auto rhs_spah = rhs->span;

        auto* new_lhs = allocExpr(AST_BINOP, SpanOver(lhs->span, rhs->span));
        new_lhs->an_Binop.op = tok_to_aop[*it];
        new_lhs->an_Binop.lhs = lhs;
        new_lhs->an_Binop.rhs = rhs;
        lhs = new_lhs;
    }

    return lhs;
}

AstExpr* Parser::parseUnaryOp() {
    auto start_span = tok.span;
    
    switch (tok.kind) {
    case TOK_AMP: {
        next();

        auto atom_expr = parseAtomExpr();
        auto* node = allocExpr(AST_ADDR, SpanOver(start_span, atom_expr->span));
        node->an_Addr.elem = atom_expr;
        return node;
    } break; 
    case TOK_STAR: {
        next();

        auto atom_expr = parseAtomExpr();
        auto* node = allocExpr(AST_DEREF, SpanOver(start_span, atom_expr->span));
        node->an_Deref.ptr = atom_expr;
        return node;
    } break;
    case TOK_MINUS: {
        next();

        auto atom_expr = parseAtomExpr();
        auto* node = allocExpr(AST_UNOP, SpanOver(start_span, atom_expr->span));
        node->an_Unop.operand = atom_expr;
        node->an_Unop.op = AOP_NEG;
        return node;
    } break;
    case TOK_NOT: {
        next();
        
        auto atom_expr = parseAtomExpr();
        auto* node = allocExpr(AST_UNOP, SpanOver(start_span, atom_expr->span));
        node->an_Unop.operand = atom_expr;
        node->an_Unop.op = AOP_NOT;
        return node;
    } break;
    case TOK_TILDE: {
        next();

        auto atom_expr = parseAtomExpr();
        auto* node = allocExpr(AST_UNOP, SpanOver(start_span, atom_expr->span));
        node->an_Unop.operand = atom_expr;
        node->an_Unop.op = AOP_BWNEG;
        return node;
    } break;
    default:
        return parseAtomExpr();
    }
}

/* -------------------------------------------------------------------------- */

AstExpr* Parser::parseAtomExpr() {
    auto root = parseAtom();

    while (true) {
        switch (tok.kind) {
        case TOK_LPAREN: {
            root = parseFuncCall(std::move(root));
        } break;
        case TOK_LBRACKET: {
            root = parseIndexOrSlice(std::move(root));
        } break;
        case TOK_DOT: {
            next();

            auto field_name_tok = wantAndGet(TOK_IDENT);

            auto* node = allocExpr(AST_FIELD, SpanOver(root->span, field_name_tok.span));
            node->an_Field.root = root;
            node->an_Field.field_name = arena.MoveStr(std::move(field_name_tok.value));
            
            root = node;
        } break;
        case TOK_LBRACE:
            if (shouldParseStructLit()) {
                root = parseStructLit(root);
                continue;
            }
            // fallthrough
        default:
            return root;
        }
    }
}

AstExpr* Parser::parseFuncCall(AstExpr* root) {
    next();

    pushAllowStructLit(true);

    std::vector<AstExpr*> args;
    if (!has(TOK_RPAREN)) {
        while (true) {
            args.emplace_back(parseExpr());

            if (has(TOK_COMMA)) {
                next();
            } else {
                break;
            }
        }
    }
    
    auto end_span = tok.span;
    want(TOK_RPAREN);

    popAllowStructLit();

    auto* node = allocExpr(AST_CALL, SpanOver(root->span, end_span));
    node->an_Call.func = root;
    node->an_Call.args = arena.MoveVec(std::move(args));
    return node;
}

AstExpr* Parser::parseIndexOrSlice(AstExpr* root) {
    next();
    auto start_span = prev.span;

    pushAllowStructLit(true);

    if (has(TOK_COLON)) {
        next();

        auto end_index = parseExpr();

        want(TOK_RBRACKET);

        auto* node = allocExpr(AST_SLICE, SpanOver(start_span, prev.span));
        node->an_Slice.array = root;
        node->an_Slice.start_index = nullptr;
        node->an_Slice.end_index = end_index;
        return node;
    }

    auto start_index = parseExpr();
    if (has(TOK_COLON)) {
        next();

        AstExpr* end_index { nullptr };
        if (!has(TOK_RBRACKET)) {
            end_index = parseExpr();
        } 

        want(TOK_RBRACKET);

        auto* node = allocExpr(AST_SLICE, SpanOver(start_span, prev.span));
        node->an_Slice.array = root;
        node->an_Slice.start_index = start_index;
        node->an_Slice.end_index = end_index;
        return node;
    }

    want(TOK_RBRACKET);

    popAllowStructLit();

    auto* node = allocExpr(AST_INDEX, SpanOver(start_span, prev.span));
    node->an_Index.array = root;
    node->an_Index.index = start_index;
    return node;
}

AstExpr* Parser::parseStructLit(AstExpr* root) {
    want(TOK_LBRACE);

    pushAllowStructLit(true);

    AstExpr* struct_lit { nullptr };
    if (has(TOK_RBRACE)) {
        next();
        struct_lit = allocExpr(AST_STRUCT_LIT_POS, SpanOver(root->span, prev.span));
        struct_lit->an_StructLitPos.root = root;
        struct_lit->an_StructLitPos.field_inits = {};
    } else if (has(TOK_IDENT)) {
        auto* first_expr = parseExpr();

        if (first_expr->kind == AST_IDENT && has(TOK_ASSIGN)) {
            next();
            auto* init_expr = parseExpr();

            std::vector<AstNamedFieldInit> field_inits({ { first_expr, init_expr } });

            if (has(TOK_COMMA)) {
                next();

                std::unordered_set<std::string_view> used_field_names;
                used_field_names.insert(first_expr->an_Ident.temp_name);
                while (true) {
                    auto ident_tok = wantAndGet(TOK_IDENT);
                    auto ident_name = arena.MoveStr(std::move(ident_tok.value));

                    auto aident = allocExpr(AST_IDENT, ident_tok.span);
                    aident->an_Ident.temp_name = ident_name;

                    if (used_field_names.contains(ident_name)) {
                        error(ident_tok.span, "field named {} initialized multiple times", ident_name);
                    } else {
                        used_field_names.insert(ident_name);
                    }

                    init_expr = parseInitializer();

                    field_inits.emplace_back(aident, init_expr);

                    if (has(TOK_COMMA)) {
                        next();
                    } else {
                        break;
                    }
                }
            }

            want(TOK_RBRACE);

            struct_lit = allocExpr(AST_STRUCT_LIT_NAMED, SpanOver(root->span, prev.span));
            struct_lit->an_StructLitNamed.root = root;
            struct_lit->an_StructLitNamed.field_inits = arena.MoveVec(std::move(field_inits));
        } else {
            std::vector<AstExpr*> field_inits({ first_expr });

            if (has(TOK_COMMA)) {
                next();

                while (true) {
                    field_inits.push_back(parseExpr());

                    if (has(TOK_COMMA)) {
                        next();
                    } else {
                        break;
                    }
                }
            }

            want(TOK_RBRACE);

            struct_lit = allocExpr(AST_STRUCT_LIT_POS, SpanOver(root->span, prev.span));
            struct_lit->an_StructLitPos.root = root;
            struct_lit->an_StructLitPos.field_inits = arena.MoveVec(std::move(field_inits));
        }   
    } else {
        auto field_inits = parseExprList();
        want(TOK_RBRACE);

        struct_lit = allocExpr(AST_STRUCT_LIT_POS, SpanOver(root->span, prev.span));
        struct_lit->an_StructLitPos.root = root;
        struct_lit->an_StructLitPos.field_inits = field_inits;
    }

    popAllowStructLit();
    return struct_lit;
}

/* -------------------------------------------------------------------------- */

bool ConvertUint(const std::string& int_str, uint64_t* value) {
    try {
        if (int_str.starts_with("0b")) {
            *value = std::stoull(int_str.substr(2), nullptr, 2);
        } else if (int_str.starts_with("0o")) {
            *value = std::stoull(int_str.substr(2), nullptr, 8);
        } else if (int_str.starts_with("0x")) {
            *value = std::stoull(int_str.substr(2), nullptr, 16);
        } else {
            *value = std::stoull(int_str);
        }

        return true;
    } catch (std::out_of_range&) {
        return false;
    }
}

static rune decodeRune(const std::string& rbytes) {
    byte b1 = rbytes[0];
    if (b1 == 0xff) {
        return -1;
    }

    int n_bytes = 0;
    int32_t r;
    if ((b1 & 0x80) == 0) { // 0xxxxxxx
        return b1;
    } else if ((b1 & 0xe0) == 0xc0) { // 110xxxxx
        n_bytes = 1;
        r = b1 & 0x1f;
    } else if ((b1 & 0xf0) == 0xe0) { // 1110xxxx
        n_bytes = 2;
        r = b1 & 0x0f;
    } else if ((b1 & 0xf8) == 0xf0) { // 11110xxx
        n_bytes = 3;
        r = b1 & 0x07;
    } else {
        Panic("utf8 decode error in parser: invalid leading byte");
    }

    if (rbytes.size() != n_bytes + 1) {
        Panic("utf8 decode error in parser: expected {} bytes but got {}", n_bytes + 1, rbytes.size());
    }

    for (int i = 0; i < n_bytes; i++) {
        if (i >= rbytes.size() - 1) {
            Panic("utf8 decode error in parser: too few bytes");
        }

        byte b = rbytes[i+1];

        r <<= 6;
        r = r | (b & 0x3f);
    }

    return r;
}

rune ConvertRuneLit(const std::string& rune_str) {
    if (rune_str[0] == '\\') {
        Assert(rune_str.size() == 2, "invalid escape code in parser: wrong char count");

        switch (rune_str[1]) {
        case 'a':
            return '\a';
        case 'b':
            return '\b';
        case 'f':
            return '\f';
        case 'n':
            return '\n';
        case 'r':
            return '\r';
        case 't':
            return '\t';
        case 'v':
            return '\v';
        case '0':
            return 0;
        case '\'': case '\"': case '\\':
            return rune_str[1];
        default:
            Panic("invalid rune literal in parser: unknown escape code");
        }
    } else {
        return decodeRune(rune_str);
    }
}

AstExpr* Parser::parseAtom() {
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
        return ident;
    } break;
    case TOK_NULL:
        next();
        return allocExpr(AST_NULL, prev.span);
    case TOK_LPAREN: {
        next();

        pushAllowStructLit(true);
        auto sub_expr = parseExpr();
        popAllowStructLit();

        want(TOK_RPAREN);

        return sub_expr;
    } break;
    case TOK_LBRACKET: 
        return parseArrayLit();
    case TOK_NEW:
        return parseNewExpr();
    case TOK_STRUCT: {
        auto start_span = tok.span;
        auto* type = parseStructTypeLabel();
        auto* astruct = allocExpr(AST_STRUCT_LIT_TYPE, SpanOver(start_span, prev.span));
        astruct->type = type;

        return parseStructLit(astruct);
    } break;
    case TOK_DOT: {
        next();

        if (has(TOK_IDENT)) {
            auto* aenum_lit_type = allocExpr(AST_ENUM_LIT_TYPE, prev.span);
            
            auto* aenum_lit = allocExpr(AST_ENUM_LIT, SpanOver(prev.span, tok.span));
            aenum_lit->an_Field.root = aenum_lit_type;
            aenum_lit->an_Field.field_name = arena.MoveStr(std::move(tok.value));
            next();

            return aenum_lit;
        }
        
        auto* astruct = allocExpr(AST_STRUCT_LIT_TYPE, prev.span);
        return parseStructLit(astruct);
    } break;
    case TOK_ATSIGN:
        return parseMacroCall();
    default:
        reject("expected expression");
        return nullptr;
    }
}

AstExpr* Parser::parseNewExpr() {
    next();
    auto start_span = prev.span;

    if (has(TOK_LBRACE)) {
        auto* astruct = allocExpr(AST_STRUCT_LIT_TYPE, prev.span);
        return parseStructPtrLit(astruct);
    }

    auto* type = parseTypeLabel();

    if (has(TOK_LBRACE) && shouldParseStructLit()) {
        auto* astruct = allocExpr(AST_STRUCT_LIT_TYPE, SpanOver(start_span, prev.span));
        astruct->type = type;
        
        return parseStructPtrLit(astruct);
    }

    AstExpr* size_expr { nullptr };
    if (has(TOK_LBRACKET)) {
        next();

        size_expr = parseExpr();

        want(TOK_RBRACKET);
    }

    auto* anew = allocExpr(AST_NEW, SpanOver(start_span, prev.span));
    anew->an_New.elem_type = type;
    anew->an_New.size_expr = size_expr;
    return anew;
}

AstExpr* Parser::parseStructPtrLit(AstExpr* root) {
    auto struct_lit = parseStructLit(root);
    if (struct_lit->kind == AST_STRUCT_LIT_POS) {
        struct_lit->kind = AST_STRUCT_PTR_LIT_POS;
    } else {
        struct_lit->kind = AST_STRUCT_PTR_LIT_NAMED;
    }

    return struct_lit;
}

AstExpr* Parser::parseArrayLit() {
    next();
    auto start_span = prev.span;

    pushAllowStructLit(true);

    std::span<AstExpr*> elems;
    if (!has(TOK_RBRACKET)) {
        elems = parseExprList();
    }

    want(TOK_RBRACKET);

    popAllowStructLit();

    auto* arr = allocExpr(AST_ARRAY, SpanOver(start_span, prev.span));
    arr->an_Array.elems = elems;
    return arr;
}

AstExpr* Parser::parseMacroCall() {
    auto start_span = tok.span;
    next();

    auto macro_ident = wantAndGet(TOK_IDENT);

    want(TOK_LPAREN);

    AstExpr* expr;
    if (macro_ident.value == "defined") {
        auto var_ident = wantAndGet(TOK_IDENT);
        want(TOK_RPAREN);

        expr = allocExpr(AST_STRING, SpanOver(start_span, prev.span));
        expr->type = &prim_string_type;
        expr->an_String.value = lookupMetaVar(var_ident.value);
    } else if (macro_ident.value == "sizeof") {
        auto* type = parseTypeLabel();
        want(TOK_RPAREN);

        expr = allocExpr(AST_MACRO_SIZEOF, SpanOver(start_span, prev.span));
        expr->type = platform_uint_type;
        expr->an_TypeMacro.type_arg = type;
    } else if (macro_ident.value == "alignof") {
        auto* type = parseTypeLabel();
        want(TOK_RPAREN);

        expr = allocExpr(AST_MACRO_ALIGNOF, SpanOver(start_span, prev.span));
        expr->type = platform_uint_type;
        expr->an_TypeMacro.type_arg = type;
    } else if (macro_ident.value == "_funcaddr") {
        auto* arg_expr = parseExpr();
        want(TOK_RPAREN);

        expr = allocExpr(AST_MACRO_FUNCADDR, SpanOver(start_span, prev.span));
        expr->type = &prim_ptr_u8_type;
        expr->an_ValueMacro.expr = arg_expr;
    } else {
        fatal(macro_ident.span, "unknown macro: {}", macro_ident.value);
    }

    return expr;
}
#include "parser.hpp"

AstNode* Parser::parseExpr() {
    auto* expr = parseBinaryOp(0);

    if (has(TOK_AS)) {
        next();

        auto dest_type = parseTypeLabel();

        auto* acast = allocNode(AST_CAST, SpanOver(expr->span, prev.span));
        acast->an_Cast.expr = expr;
        acast->an_Cast.dest_type = dest_type;
        expr = acast;
    }

    if (has(TOK_MATCH)) {
        next();

        auto* pattern = parseCasePattern();

        auto* amatch = allocNode(AST_TEST_MATCH, SpanOver(expr->span, prev.span));
        amatch->an_TestMatch.expr = expr;
        amatch->an_TestMatch.pattern = pattern;
        expr = amatch;
    }

    return expr;
}

/* -------------------------------------------------------------------------- */

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

AstNode* Parser::parseBinaryOp(int pred_level) {
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

        auto op_tok = tok;
        next();

        auto rhs = parseBinaryOp(pred_level + 1);

        auto lhs_span = lhs->span;
        auto rhs_spah = rhs->span;

        auto* new_lhs = allocNode(AST_BINOP, SpanOver(lhs->span, rhs->span));
        new_lhs->an_Binop.op = {op_tok.span, op_tok.kind};
        new_lhs->an_Binop.lhs = lhs;
        new_lhs->an_Binop.rhs = rhs;
        lhs = new_lhs;
    }

    return lhs;
}

AstNode* Parser::parseUnaryOp() {
    auto start_span = tok.span;
    
    switch (tok.kind) {
    case TOK_AMP: {
        next();

        auto atom_expr = parseAtomExpr();
        auto* node = allocNode(AST_ADDR, SpanOver(start_span, atom_expr->span));
        node->an_Addr.expr = atom_expr;
        return node;
    } break; 
    case TOK_STAR: {
        next();

        auto atom_expr = parseAtomExpr();
        auto* node = allocNode(AST_DEREF, SpanOver(start_span, atom_expr->span));
        node->an_Deref.expr = atom_expr;
        return node;
    } break;
    case TOK_MINUS: 
    case TOK_NOT:
    case TOK_TILDE: {
        auto op_tok = tok;
        next();

        auto atom_expr = parseAtomExpr();
        auto* node = allocNode(AST_UNOP, SpanOver(start_span, atom_expr->span));
        node->an_Unop.expr = atom_expr;
        node->an_Unop.op = { op_tok.span, op_tok.kind };
        return node;
    } break;
    default:
        return parseAtomExpr();
    }
}

/* -------------------------------------------------------------------------- */

AstNode* Parser::parseAtomExpr() {
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

            auto* node = allocNode(AST_SELECTOR, SpanOver(root->span, field_name_tok.span));
            node->an_Sel.expr = root;
            node->an_Sel.field_name = ast_arena.MoveStr(std::move(field_name_tok.value));
            
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

AstNode* Parser::parseFuncCall(AstNode* func) {
    next();

    pushAllowStructLit(true);

    AstNode* args { nullptr };
    if (!has(TOK_RPAREN)) {
        args = parseExprList();
    }
    
    auto end_span = tok.span;
    want(TOK_RPAREN);

    popAllowStructLit();

    auto* node = allocNode(AST_CALL, SpanOver(func->span, end_span));
    node->an_Call.func = func;
    node->an_Call.args = args;
    return node;
}

AstNode* Parser::parseIndexOrSlice(AstNode* root) {
    next();
    auto start_span = prev.span;

    pushAllowStructLit(true);

    if (has(TOK_COLON)) {
        next();

        auto end_index = parseExpr();

        want(TOK_RBRACKET);

        auto* node = allocNode(AST_SLICE, SpanOver(start_span, prev.span));
        node->an_Slice.expr = root;
        node->an_Slice.start_index = nullptr;
        node->an_Slice.end_index = end_index;
        return node;
    }

    auto start_index = parseExpr();
    if (has(TOK_COLON)) {
        next();

        AstNode* end_index { nullptr };
        if (!has(TOK_RBRACKET)) {
            end_index = parseExpr();
        } 

        want(TOK_RBRACKET);

        auto* node = allocNode(AST_SLICE, SpanOver(start_span, prev.span));
        node->an_Slice.expr = root;
        node->an_Slice.start_index = start_index;
        node->an_Slice.end_index = end_index;
        return node;
    }

    want(TOK_RBRACKET);

    popAllowStructLit();

    auto* node = allocNode(AST_INDEX, SpanOver(start_span, prev.span));
    node->an_Index.expr = root;
    node->an_Index.index = start_index;
    return node;
}

AstNode* Parser::parseStructLit(AstNode* type) {
    want(TOK_LBRACE);

    pushAllowStructLit(true);

    AstNode* struct_lit { nullptr };
    if (has(TOK_RBRACE)) {
        next();
        struct_lit = allocNode(AST_STRUCT_LIT, SpanOver(type->span, prev.span));
        struct_lit->an_StructLit.type = type;
        struct_lit->an_StructLit.field_inits = {};
    } else if (has(TOK_IDENT)) {
        auto* first_expr = parseExpr();

        std::vector<AstNode*> field_inits;
        if (first_expr->kind == AST_IDENT && has(TOK_ASSIGN)) {
            next();
            auto* init_expr = parseExpr();

            auto* field_init = allocNode(AST_NAMED_INIT, SpanOver(first_expr->span, init_expr->span));
            field_init->an_NamedInit.name = first_expr->an_Ident.name;
            field_init->an_NamedInit.init = init_expr;
            field_inits.push_back(field_init);

            if (has(TOK_COMMA)) {
                next();

                std::unordered_set<std::string_view> used_field_names;
                used_field_names.insert(first_expr->an_Ident.name);
                while (true) {
                    auto ident_tok = wantAndGet(TOK_IDENT);
                    auto field_name = ast_arena.MoveStr(std::move(ident_tok.value));

                    if (used_field_names.contains(field_name)) {
                        error(ident_tok.span, "field named {} initialized multiple times", field_name);
                    } else {
                        used_field_names.insert(field_name);
                    }

                    init_expr = parseInitializer();

                    field_init = allocNode(AST_NAMED_INIT, SpanOver(ident_tok.span, init_expr->span));
                    field_init->an_NamedInit.name = field_name;
                    field_init->an_NamedInit.init = init_expr;
                    field_inits.emplace_back(field_init);

                    if (has(TOK_COMMA)) {
                        next();
                    } else {
                        break;
                    }
                }
            }

            want(TOK_RBRACE);
        } else {
            std::vector<AstNode*> field_inits({ first_expr });

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
        }

        struct_lit = allocNode(AST_STRUCT_LIT, SpanOver(type->span, prev.span));
        struct_lit->an_StructLit.type = type;
        struct_lit->an_StructLit.field_inits =  ast_arena.MoveVec(std::move(field_inits)); 
    } else {
        auto field_inits = parseExprList();
        want(TOK_RBRACE);

        struct_lit = allocNode(AST_STRUCT_LIT, SpanOver(type->span, prev.span));
        struct_lit->an_StructLit.type = type;
        struct_lit->an_StructLit.field_inits = field_inits->an_ExprList.exprs;
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

AstNode* Parser::parseAtom() {
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
        ident->an_Ident.symbol = nullptr;
        return ident;
    } break;
    case TOK_NULL:
        next();
        return allocNode(AST_NULL, prev.span);
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

        return parseStructLit(type);
    } break;
    case TOK_DOT: {
        next();
        auto* adot = allocNode(AST_DOT, prev.span);

        if (has(TOK_IDENT)) {
            next();

            auto* asel = allocNode(AST_SELECTOR, SpanOver(adot->span, prev.span));
            asel->an_Sel.field_name = ast_arena.MoveStr(std::move(prev.value));

            return asel;
        }
        
        return parseStructLit(adot);
    } break;
    case TOK_ATSIGN:
        return parseMacroCall();
    default:
        reject("expected expression");
        return nullptr;
    }
}

AstNode* Parser::parseNewExpr() {
    next();
    auto start_span = prev.span;

    if (has(TOK_LBRACE)) {
        auto* adot = allocNode(AST_DOT, start_span);

        auto* struct_lit = parseStructLit(adot);
        struct_lit->kind = AST_NEW_STRUCT;
        return struct_lit;
    }

    auto* type = parseTypeLabel();

    if (has(TOK_LBRACE) && shouldParseStructLit()) {        
        auto* struct_lit =  parseStructLit(type);
        struct_lit->kind = AST_NEW_STRUCT;
        return struct_lit;
    }

    if (has(TOK_LBRACKET)) {
        next();

        auto* len_expr = parseExpr();

        want(TOK_RBRACKET);

        auto* anew_array = allocNode(AST_NEW_ARRAY, SpanOver(start_span, prev.span));
        anew_array->an_NewArray.type = type;
        anew_array->an_NewArray.len = len_expr;
        return anew_array;
    } 

    auto* anew = allocNode(AST_NEW, SpanOver(start_span, prev.span));
    anew->an_New.type = type;
    return anew;
}

AstNode* Parser::parseArrayLit() {
    next();
    auto start_span = prev.span;

    pushAllowStructLit(true);

    AstNode* items = parseExprList();
    items->kind = AST_ARRAY_LIT;

    want(TOK_RBRACKET);

    popAllowStructLit();

    return items;
}

AstNode* Parser::parseMacroCall() {
    auto start_span = tok.span;
    next();

    auto macro_ident = wantAndGet(TOK_IDENT);

    want(TOK_LPAREN);

    AstNode* expr;
    if (macro_ident.value == "defined") {
        auto var_ident = wantAndGet(TOK_IDENT);
        want(TOK_RPAREN);

        expr = allocNode(AST_STRING_LIT, SpanOver(start_span, prev.span));
        expr->an_String.value = lookupMetaVar(var_ident.value);
    } else if (macro_ident.value == "sizeof") {
        auto* type = parseTypeLabel();
        want(TOK_RPAREN);

        expr = allocNode(AST_MACRO_SIZEOF, SpanOver(start_span, prev.span));
        expr->an_Macro.arg = type;
    } else if (macro_ident.value == "alignof") {
        auto* type = parseTypeLabel();
        want(TOK_RPAREN);

        expr = allocNode(AST_MACRO_ALIGNOF, SpanOver(start_span, prev.span));
        expr->an_Macro.arg = type;
    } else if (macro_ident.value == "_funcaddr") {
        auto* arg_expr = parseExpr();
        want(TOK_RPAREN);

        expr = allocNode(AST_MACRO_FUNCADDR, SpanOver(start_span, prev.span));
        expr->an_Macro.arg = arg_expr;
    } else {
        fatal(macro_ident.span, "unknown macro: {}", macro_ident.value);
    }

    return expr;
}
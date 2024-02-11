#include "parser.hpp"

std::unique_ptr<AstExpr> Parser::parseExpr() {
    auto bin_op = parseBinaryOp(0);

    if (has(TOK_AS)) {
        next();

        auto dest_type = parseTypeLabel();

        auto bop_span = bin_op->span;
        return std::make_unique<AstCast>(
            SpanOver(bop_span, prev.span),
            std::move(bin_op),
            dest_type
        );
    }

    return bin_op;
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

std::unique_ptr<AstExpr> Parser::parseBinaryOp(int pred_level) {
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

        lhs = std::make_unique<AstBinaryOp>(
            SpanOver(lhs->span, rhs->span),
            tok_to_aop[*it],
            std::move(lhs),
            std::move(rhs)
        );
    }

    return lhs;
}

std::unique_ptr<AstExpr> Parser::parseUnaryOp() {
    auto start_span = tok.span;
    
    switch (tok.kind) {
    case TOK_AMP: {
        next();

        auto atom_expr = parseAtomExpr();
        auto span = SpanOver(start_span, atom_expr->span);
        return std::make_unique<AstAddrOf>(span, std::move(atom_expr));
    } break; 
    case TOK_STAR: {
        next();

        auto atom_expr = parseAtomExpr();
        auto span = SpanOver(start_span, atom_expr->span);
        return std::make_unique<AstDeref>(span, std::move(atom_expr));
    } break;
    case TOK_MINUS: {
        next();

        auto atom_expr = parseAtomExpr();
        auto span = SpanOver(start_span, atom_expr->span);
        return std::make_unique<AstUnaryOp>(span, AOP_NEG, std::move(atom_expr));
    } break;
    case TOK_NOT: {
        next();
        
        auto atom_expr = parseAtomExpr();
        auto span = SpanOver(start_span, atom_expr->span);
        return std::make_unique<AstUnaryOp>(span, AOP_NOT, std::move(atom_expr));
    } break;
    default:
        return parseAtomExpr();
    }
}

/* -------------------------------------------------------------------------- */

std::unique_ptr<AstExpr> Parser::parseAtomExpr() {
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

            auto start_span = root->span;
            root = std::make_unique<AstFieldAccess>(
                SpanOver(start_span, field_name_tok.span),
                std::move(root),
                std::move(field_name_tok.value)
            );
        } break;
        default:
            return root;
        }
    }
}

std::unique_ptr<AstExpr> Parser::parseFuncCall(std::unique_ptr<AstExpr>&& root) {
    next();

    std::vector<std::unique_ptr<AstExpr>> args;
    if (!has(TOK_RPAREN)) {
        while (true) {
            if (has(TOK_NULL)) {
                next();

                args.emplace_back(std::make_unique<AstNullLit>(prev.span));
            } else {
                args.emplace_back(parseExpr());
            }

            if (has(TOK_COMMA)) {
                next();
            } else {
                break;
            }
        }
    }
    
    auto end_span = tok.span;
    want(TOK_RPAREN);

    auto start_span = root->span;
    return std::make_unique<AstCall>(SpanOver(start_span, end_span), std::move(root), std::move(args));
}

std::unique_ptr<AstExpr> Parser::parseIndexOrSlice(std::unique_ptr<AstExpr>&& root) {
    next();
    auto start_span = prev.span;

    if (has(TOK_COLON)) {
        next();

        auto end_index = parseExpr();

        want(TOK_RBRACKET);

        return std::make_unique<AstSlice>(
            SpanOver(start_span, prev.span), 
            std::move(root), 
            nullptr, 
            std::move(end_index)
        );
    }

    auto start_index = parseExpr();
    if (has(TOK_COLON)) {
        next();

        std::unique_ptr<AstExpr> end_index { nullptr };
        if (!has(TOK_RBRACKET)) {
            end_index = parseExpr();
        } 

        want(TOK_RBRACKET);

        return std::make_unique<AstSlice>(
            SpanOver(start_span, prev.span),
            std::move(root),
            std::move(start_index),
            std::move(end_index)
        );
    }

    want(TOK_RBRACKET);

    return std::make_unique<AstIndex>(
        SpanOver(start_span, prev.span),
        std::move(root),
        std::move(start_index)
    );
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

static rune convertRuneLit(const std::string& rune_str) {
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

std::unique_ptr<AstExpr> Parser::parseAtom() {
    switch (tok.kind) {
    case TOK_INTLIT: {
        next();

        uint64_t value = 0;
        if (!ConvertUint(prev.value, &value)) {
            error(prev.span, "integer literal is too big to be represented by any integer type");
        }

        return std::make_unique<AstIntLit>(prev.span, value);
    } break;
    case TOK_FLOATLIT: {
        next();

        double value = 0;
        try {
            value = std::stod(prev.value);
        } catch (std::out_of_range&) {
            error(prev.span, "float literal cannot be accurately represented by any float type");
        }

        return std::make_unique<AstFloatLit>(prev.span, value);
    } break;
    case TOK_RUNELIT: {
        next();

        return std::make_unique<AstIntLit>(prev.span, &prim_i32_type, convertRuneLit(prev.value));
    } break;
    case TOK_BOOLLIT: {
        next();

        return std::make_unique<AstBoolLit>(prev.span, prev.value == "true");
    } break;
    case TOK_STRLIT: {
        next();

        return std::make_unique<AstStringLit>(prev.span, std::move(prev.value));
    } break;
    case TOK_IDENT: {
        auto name_tok = tok;
        next();

        return std::make_unique<AstIdent>(name_tok.span, std::move(name_tok.value));
    } break;
    case TOK_LPAREN: {
        next();

        auto sub_expr = parseExpr();

        want(TOK_RPAREN);

        return sub_expr;
    } break;
    case TOK_LBRACKET: 
        return parseArrayLit();
    default:
        reject("expected expression");
        return nullptr;
    }
}

std::unique_ptr<AstArrayLit> Parser::parseArrayLit() {
    next();
    auto start_span = prev.span;

    auto elems = parseExprList();

    want(TOK_RBRACKET);

    return std::make_unique<AstArrayLit>(SpanOver(start_span, prev.span), std::move(elems));
}
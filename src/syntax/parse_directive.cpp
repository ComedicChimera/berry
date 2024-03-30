#include "parser.hpp"

void Parser::parseDirective() {
    if (tok.value == "if") {
        meta_if_depth++;
        parseMetaIfDirective();
    } else if (tok.value == "elif") {
        if (meta_if_depth > 0) {
            parseMetaIfDirective();
        } else {
            reject("#elif directive without preceding #if");
        }
    } else if (tok.value == "end") {
        if (meta_if_depth > 0) {
            meta_if_depth--;
            next();
        } else {
            reject("unbalanced #end directive");
        }
    } else if (tok.value == "require") {
        next();

        want(TOK_LPAREN);

        if (evaluateMetaExpr().size() == 0) {
            // Not an actual error (nothing is reported), but just an easy way
            // to quickly jump out of parsing.
            throw CompileError{};
        }

        want(TOK_RPAREN);
    } else {
        reject("invalid directive: {}", tok.value);
    }
}

void Parser::parseMetaIfDirective() {
    want(TOK_LPAREN);

    auto expr_result = evaluateMetaExpr();

    want(TOK_RPAREN);

    if (expr_result.size() == 0) {
        // Skip until we either reach the end of the file or find a matching
        // #end token for the #if directive.
        int nested_ifs = 0;
        while (tok.kind != TOK_EOF) {
            if (tok.kind == TOK_DIRECTIVE) {
                if (tok.value == "if") {
                    nested_ifs++;
                } else if (tok.value == "end") {
                    if (nested_ifs > 0) {
                        nested_ifs--;
                    } else {
                        next();
                        break;
                    }
                }
            }

            next();
        }
    }
}

/* -------------------------------------------------------------------------- */

std::string Parser::evaluateMetaExpr() {
    auto lhs = evaluateMetaAndExpr();

    while (has(TOK_OR)) {
        next();

        auto rhs = evaluateMetaAndExpr();

        if (lhs.size() > 0 || rhs.size() > 0)
            lhs = "true";
        else
            lhs = "";
    }

    return lhs;
}

std::string Parser::evaluateMetaAndExpr() {
    auto lhs = evaluateMetaEqExpr();

    while (has(TOK_AND)) {
        next();

        auto rhs = evaluateMetaUnaryExpr();

        if (lhs.size() > 0 && rhs.size() > 0)
            lhs = "true";
        else
            lhs = "";
    }

    return lhs;
}

std::string Parser::evaluateMetaEqExpr() {
    auto lhs = evaluateMetaUnaryExpr();

    while (true) {
        if (has(TOK_EQ)) {
            next();

            auto rhs = evaluateMetaUnaryExpr();
            lhs = lhs == rhs ? "true" : "";
        } else if (has(TOK_NE)) {
            next();

            auto rhs = evaluateMetaUnaryExpr();
            lhs = lhs != rhs ? "true" : "";
        } else {
            break;
        }
    }

    return lhs;
}

std::string Parser::evaluateMetaUnaryExpr() {
    if (has(TOK_NOT)) {
        next();

        auto value = evaluateMetaValue();
        if (value.size() > 0) {
            return "";
        } else {
            return "true";
        }
    }

    return evaluateMetaValue();
}

std::string Parser::evaluateMetaValue() {
    switch (tok.kind) {
    case TOK_IDENT:
        next();
        return std::string(lookupMetaVar(prev.value));
    case TOK_STRING:
    case TOK_INTLIT:
        next();
        return prev.value;
    case TOK_BOOL:
        next();
        if (prev.value == "false")
            return "";
        else
            return "true";
    case TOK_LPAREN: {
        next();
        auto value = evaluateMetaExpr();
        want(TOK_RPAREN);
        return value;
    } break;
    default:
        reject("expected meta expression");
        break;
    }

    return {};
}

std::string_view Parser::lookupMetaVar(const std::string& name) {
    if (prev.value == "OS") {
        return platform_meta_vars.os;
    } else if (prev.value == "ARCH") {
        return platform_meta_vars.arch;
    } else if (prev.value == "ARCH_SIZE") {
        return platform_meta_vars.arch_size;
    } else if (prev.value == "DEBUG") {
        return platform_meta_vars.debug;
    } else if (prev.value == "COMPILER") {
        return "berryc";
    } else {
        return "";
    }
}
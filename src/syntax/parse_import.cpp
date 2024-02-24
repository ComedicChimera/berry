#include "parser.hpp"

Token Parser::ParseModuleName() {
    if (has(TOK_MODULE)) {
        next();

        auto name = wantAndGet(TOK_IDENT);
        want(TOK_SEMI);

        return name;
    }

    // No name specified.
    return { TOK_EOF };
}

/* -------------------------------------------------------------------------- */

void Parser::parseImportStmt() {
    next();

    if (has(TOK_LPAREN)) {
        next();

        while (true) {
            parseModulePath();

            if (has(TOK_COMMA)) {
                next();
            } else {
                break;
            }
        }

        want(TOK_RPAREN);
    } else {
        parseModulePath();
    }

    want(TOK_SEMI);
}

void Parser::parseModulePath() {
    auto mod_path = parseIdentList(TOK_DOT);

    auto imported_name_tok = mod_path.back();
    if (has(TOK_AS)) {
        next();

        imported_name_tok = wantAndGet(TOK_IDENT);
    }

    auto it = src_file.import_table.find(imported_name_tok.value);
    if (it == src_file.import_table.end()) {
        auto dep_id = findOrAddModuleDep(mod_path);

        auto imported_name = arena.MoveStr(std::move(imported_name_tok.value));
        src_file.import_table[imported_name] = dep_id;
    } else {
        error(imported_name_tok.span, "multiple imports with name {}", imported_name_tok.value);
    }
}

size_t Parser::findOrAddModuleDep(const std::vector<Token>& tok_mod_path) {
    std::vector<std::string> mod_path;
    for (auto& tok : tok_mod_path) {
        mod_path.emplace_back(std::move(tok.value));
    }
    
    Module::ImportLoc import_loc { src_file, SpanOver(tok_mod_path.front().span, tok_mod_path.back().span) };
    bool paths_matched = true;
    for (size_t i = 0; i < src_file.parent->deps.size(); i++) {
        auto& dep = src_file.parent->deps[i];

        if (dep.mod_path.size() != mod_path.size()) {
            continue;
        }

        for (size_t j = 0; j < mod_path.size(); j++) {
            if (mod_path[j] != dep.mod_path[j]) {
                paths_matched = false;
                break;
            }
        }

        if (paths_matched) {
            dep.import_locs.push_back(import_loc);
            return i;
        }
    }

    auto& dep = src_file.parent->deps.emplace_back(std::move(mod_path), std::move(import_loc));
    dep.import_locs.push_back(import_loc);
    return src_file.parent->deps.size() - 1;
}
#include "parser.hpp"

Token Parser::ParseModuleName() {
    next();

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

        auto imported_name = global_arena.MoveStr(std::move(imported_name_tok.value));
        src_file.import_table.emplace(imported_name, dep_id);

        src_file.parent->deps[dep_id].import_locs.emplace_back(
            src_file.file_number, 
            SpanOver(mod_path[0].span, mod_path.back().span)
        );
    } else {
        error(imported_name_tok.span, "multiple imports with name {}", imported_name_tok.value);
    }
}

size_t Parser::findOrAddModuleDep(const std::vector<Token>& tok_mod_path) {
    std::vector<std::string> mod_path;
    for (auto& tok : tok_mod_path) {
        mod_path.emplace_back(std::move(tok.value));
    }
    
    bool paths_matched = true;
    int i = 0;
    for (auto& dep : src_file.parent->deps) {
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
            return i;
        }

        i++;
    }

    src_file.parent->deps.emplace_back(std::move(mod_path));
    return src_file.parent->deps.size() - 1;
}
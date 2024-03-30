#include <unordered_set>

#include "checker.hpp"

void Checker::checkDef(AstDef* def) {
    switch (def->kind) {
    case AST_FUNC:
        checkFuncDef(def);
        break;
    case AST_GLVAR:
        checkGlobalVarDef(def);
        break;
    case AST_STRUCT:
        checkStructDef(def);
        break;
    case AST_ALIAS:
        checkAliasDef(def);
        break;
    case AST_ENUM:
        checkEnumDef(def);
        break;
    default:
        Panic("checking is not implemented for def {}", (int)def->kind);
    }
}

/* -------------------------------------------------------------------------- */

void Checker::checkFuncDef(AstDef* node) {   
    checkFuncAttrs(node);

    pushScope();

    for (auto* param : node->an_Func.params) {
        declareLocal(param);
    }

    auto& fd = node->an_Func;
    if (fd.body != nullptr) {
        enclosing_return_type = fd.return_type;
        bool always_returns = checkStmt(fd.body);
        enclosing_return_type = nullptr;

        if (fd.return_type->kind != TYPE_UNIT && !always_returns) {
            error(fd.body->span, "function must return a value");
        }
    }

    popScope();
}

void Checker::checkGlobalVarDef(AstDef* node) {
    checkGlobalVarAttrs(node);

    auto& gl_var = node->an_GlVar;

    if (gl_var.init_expr) {
        is_comptime_expr = true;
        checkExpr(gl_var.init_expr, gl_var.symbol->type);

        if (gl_var.symbol->flags & SYM_COMPTIME && !is_comptime_expr) {
            error(node->span, "constant initializer must be computable at compile-time");
        }

        gl_var.const_value = is_comptime_expr ? CONST_VALUE_MARKER : nullptr;

        mustSubType(gl_var.init_expr->span, gl_var.init_expr->type, gl_var.symbol->type);

        finishExpr();
    }
}

/* -------------------------------------------------------------------------- */

void Checker::checkStructDef(AstDef* node) {
    TypeCycle cycle;
    if (checkForInfType(node->an_Struct.symbol->type, cycle)) {
        fatalOnTypeCycle(node->an_Struct.symbol->span, cycle);
    }

    // TODO: check field attrs
}

void Checker::checkAliasDef(AstDef* node) {
    TypeCycle cycle;
    if (checkForInfType(node->an_Alias.symbol->type, cycle)) {
        fatalOnTypeCycle(node->an_Alias.symbol->span, cycle);
    }

    // TODO: check field attrs
}

bool Checker::checkForInfType(Type* type, TypeCycle& cycle) {
    switch (type->kind) {
    case TYPE_NAMED: 
        if (type->ty_Named.mod_id == mod.id) {
            auto it = type_explore_table.find(type->ty_Named.name);

            if (it == type_explore_table.end()) {
                type_explore_table.emplace(type->ty_Named.name, true);

                bool is_cycle = checkForInfType(type->ty_Named.type, cycle);
                if (is_cycle && !cycle.done) {
                    cycle.nodes.push_back(type);

                    if (cycle.nodes.front()->ty_Named.name == type->ty_Named.name) {
                        cycle.done = true;
                    }
                }

                type_explore_table[type->ty_Named.name] = false;
                return is_cycle;
            } else if (it->second) {
                // Cycle!
                cycle.nodes.push_back(type);
                return true;
            }
        } 
        break;
    case TYPE_STRUCT:
        for (auto& field : type->ty_Struct.fields) {
            if (checkForInfType(field.type, cycle)) {
                return true;
            }
        }
        break;
    }

    return false;
}

void Checker::fatalOnTypeCycle(const TextSpan& span, TypeCycle& cycle) {
    std::string fmt_cycle;
    while (!cycle.nodes.empty()) {
        auto node = cycle.nodes.back();
        cycle.nodes.pop_back();

        if (fmt_cycle.size() > 0) {
            fmt_cycle += " -> ";
        }

        fmt_cycle += node->ty_Named.name;
    }

    fatal(span, "infinite type detected: {}", fmt_cycle);
}

/* -------------------------------------------------------------------------- */

void Checker::checkEnumDef(AstDef* def) {
    for (auto& init : def->an_Enum.variant_inits) {
        if (init.init_expr) {
            is_comptime_expr = true;
            checkExpr(init.init_expr);

            mustIntType(init.init_expr->span, init.init_expr->type);

            if (!is_comptime_expr) {
                error(init.init_expr->span, "enum variant initializer must be computable at compile-time");
            }
        }
    }
}

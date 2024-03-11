#include <unordered_set>

#include "checker.hpp"

void Checker::checkDef(AstDef* def) {
    switch (def->kind) {
    case AST_FUNC:
        checkFuncDef(def);
        break;
    case AST_STRUCT:
        checkStructDef(def);
        break;
    default:
        Panic("checking is not implemented for def {}", (int)def->kind);
    }
}

/* -------------------------------------------------------------------------- */

struct SpecialMetadataInfo {
    std::unordered_set<std::string_view> expected_values;
    bool check_values;
    bool expect_body;
};

static std::unordered_map<std::string_view, SpecialMetadataInfo> special_metadata_table[] = {
    {
        { "extern", { {}, false, false } },
        { "abientry", { {}, false, true } },
        { "callconv", { { "c", "win64", "stdcall" }, true, true }}
    }, // FuncDef
    {
        { "extern", { {}, false, false } },
        { "abientry", { {}, false, true } }
    },  // Global Variables
    {}  // Structs
};

bool Checker::checkMetadata(const Metadata& metadata, AstKind meta_kind) {
    auto& special_meta = special_metadata_table[meta_kind];
    bool expect_body = true;
    for (auto& tag : metadata) {
        auto it = special_meta.find(tag.name);
        if (it != special_meta.end()) {
            auto& meta_info = it->second;

            if (meta_info.expected_values.size() == 0) {
                if (tag.value.size() != 0) {
                    error(tag.value_span, "special metadata tag {} does not accept a value", tag.name);
                }
            } else {
                if (tag.value.size() == 0) {
                    error(tag.value_span, "special metadata tag {} requires a value", tag.name);
                } else if (meta_info.check_values && !meta_info.expected_values.contains(tag.value)) {
                    error(tag.value_span, "invalid value {} for special metadata tag {}", tag.value, tag.name);
                }
            }

            expect_body = expect_body && meta_info.expect_body;
        }
    }

    return expect_body; 
}

/* -------------------------------------------------------------------------- */

void Checker::checkFuncDef(AstDef* node) {    
    bool expect_body = checkMetadata(node->metadata, node->kind);
    if (expect_body && node->an_Func.body == nullptr) {
        error(node->span, "function {} must have a body", node->an_Func.symbol->name);
    } else if (!expect_body && node->an_Func.body != nullptr) {
        error(node->span, "function {} is externally defined and can't have a body", node->an_Func.symbol->name);
    }

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

void Checker::checkGlobalVar(AstDef* node) {
    auto& gl_var = node->an_GlVar;

    bool expect_init = checkMetadata(node->metadata, node->kind);
    if (!expect_init && gl_var.init_expr != nullptr) {
        error(node->span, "external global variable {} cannot have an initializer", gl_var.symbol->name);
    }

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


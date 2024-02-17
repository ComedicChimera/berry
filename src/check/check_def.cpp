#include <unordered_set>

#include "checker.hpp"

void Checker::CheckDef(AstDef* def) {
    checkMetadata(def);

    switch (def->kind) {
    case AST_FUNC:
        checkFuncDef(def);
        break;
    case AST_GLOBAL_VAR:
        checkGlobalVar(def);
        break;
    default:
        Panic("checking is not implemented for {}", (int)def->kind);
    }
}

/* -------------------------------------------------------------------------- */

static std::unordered_map<std::string_view, std::unordered_set<std::string_view>> special_metadata_table[] = {
    {
        { "extern", {} },
        { "abientry", {} },
        { "callconv", { "c", "win64", "stdcall" }}
    }, // FuncDef
    {
        { "extern", {} },
        { "abientry", {} }
    }  // Global Variables
};

void Checker::checkMetadata(AstDef* def) {
    auto& special_meta = special_metadata_table[def->kind];
    bool expect_body = true;
    for (auto& tag : def->metadata) {
        auto it = special_meta.find(tag.name);
        if (it != special_meta.end()) {
            auto& expected_values = it->second;

            if (expected_values.size() == 0) {
                if (tag.value.size() != 0) {
                    error(tag.value_span, "special metadata tag {} does not accept a value", tag.name);
                }
            } else {
                if (tag.value.size() == 0) {
                    error(tag.value_span, "special metadata tag {} requires a value", tag.name);
                } else if (!expected_values.contains(tag.value)) {
                    error(tag.value_span, "invalid value {} for special metadata tag {}", tag.value, tag.name);
                }
            }
        }
    }

    if (def->kind == AST_FUNC) {
        if (expect_body && def->an_Func.body == nullptr) {
            error(def->span, "function {} must have a body", def->an_Func.symbol->name);
        } else if (!expect_body && def->an_Func.body != nullptr) {
            error(def->span, "function {} is externally defined and can't have a body", def->an_Func.symbol->name);
        }
    } else if (def->kind == AST_GLOBAL_VAR) {
        if (!expect_body && def->an_GlobalVar.init != nullptr) {
            error(def->span, "external global variable {} cannot have an initializer", def->an_GlobalVar.symbol->name);
        }
    }    
}

/* -------------------------------------------------------------------------- */

void Checker::checkFuncDef(AstDef* node) {    
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
    auto& gv = node->an_GlobalVar;

    if (gv.init) {
        checkExpr(gv.init);

        mustSubType(gv.init->span, gv.init->type, gv.symbol->type);

        finishExpr();
    }
}

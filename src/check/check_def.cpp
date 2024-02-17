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
    }  // Global Variables
};

void Checker::checkMetadata(AstDef* def) {
    auto& special_meta = special_metadata_table[def->kind];
    bool expect_body = true;
    for (auto& tag : def->metadata) {
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

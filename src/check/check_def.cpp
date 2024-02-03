#include <unordered_set>

#include "checker.hpp"

static std::unordered_map<std::string, bool> special_func_metadata {
    { "extern", false },
    { "abientry", false },
    { "callconv", true }
};

static std::unordered_set<std::string> supported_callconvs {
    "c", "win64", "stdcall"
};

void Checker::checkFuncMetadata(AstFuncDef& fd) {
    bool expect_body = true;

    for (auto& pair : fd.metadata) {
        auto& tag = pair.second;

        auto it = special_func_metadata.find(tag.name);
        if (it != special_func_metadata.end()) {
            if (it->second) {
                if (tag.value.size() == 0 && it->second) {
                    Error(tag.name_span, "metadata tag {} expects a value", tag.name);
                    continue;
                }

                if (tag.name == "callconv") {
                    if (!supported_callconvs.contains(tag.value)) {
                        Error(tag.value_span, "unsupported calling convention: {}", tag.value);
                    }
                }
            } else if (tag.value.size() > 0 && !it->second) {
                Error(tag.value_span, "metadata tag {} does not expect a value", tag.name);
            }

            if (tag.name == "extern") {
                expect_body = false;
            }
        }
    }

    if (expect_body && fd.body == nullptr) {
        Error(fd.span, "function {} must have a body", fd.symbol->name);
    } else if (!expect_body && fd.body != nullptr) {
        Error(fd.span, "function {} is externally defined and can't have a body", fd.symbol->name);
    }
}

void Checker::Visit(AstFuncDef& node) {
    checkFuncMetadata(node);
    
    PushScope();

    for (auto* param : node.params) {
        DeclareLocal(param);
    }

    if (node.body != nullptr) {
        PushScope();

        visitNode(node.body);

        PopScope();
    }


    PopScope();
}   

/* -------------------------------------------------------------------------- */

void Checker::Visit(AstGlobalVarDef& node) {
    // TODO: check metadata attrs (not right now boys)

    if (node.var_def->init) {
        visitNode(node.var_def->init);

        MustSubType(node.var_def->init->span, node.var_def->init->type, node.var_def->symbol->type);

        FinishExpr();
    }
}

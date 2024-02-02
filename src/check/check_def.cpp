#include <unordered_set>

#include "ast.hpp"
#include "checker.hpp"

static std::unordered_map<std::string, bool> special_func_metadata {
    { "extern", false },
    { "abientry", false },
    { "callconv", true }
};

static std::unordered_set<std::string> supported_callconvs {
    "c", "win64", "stdcall"
};

static void checkFuncMetadata(Checker& c, AstFuncDef* fd) {
    bool expect_body = true;

    for (auto& pair : fd->metadata) {
        auto& tag = pair.second;

        auto it = special_func_metadata.find(tag.name);
        if (it != special_func_metadata.end()) {
            if (it->second) {
                if (tag.value.size() == 0 && it->second) {
                    c.Error(tag.name_span, "metadata tag {} expects a value", tag.name);
                    continue;
                }

                if (tag.name == "callconv") {
                    if (!supported_callconvs.contains(tag.value)) {
                        c.Error(tag.value_span, "unsupported calling convention: {}", tag.value);
                    }
                }
            } else if (tag.value.size() > 0 && !it->second) {
                c.Error(tag.value_span, "metadata tag {} does not expect a value", tag.name);
            }

            if (tag.name == "extern") {
                expect_body = false;
            }
        }
    }

    if (expect_body && fd->body == nullptr) {
        c.Error(fd->span, "function {} must have a body", fd->symbol->name);
    } else if (!expect_body && fd->body != nullptr) {
        c.Error(fd->span, "function {} is externally defined and can't have a body", fd->symbol->name);
    }
}

void AstFuncDef::Check(Checker& c) {
    checkFuncMetadata(c, this);
    
    c.PushScope();

    for (auto* param : params) {
        c.DeclareLocal(param);
    }

    if (body != nullptr) {
        c.PushScope();

        body->Check(c);

        c.PopScope();
    }


    c.PopScope();
}   

/* -------------------------------------------------------------------------- */

void AstGlobalVarDef::Check(Checker& c) {
    // TODO: check metadata attrs (not right now boys)

    if (var_def->init) {
        var_def->init->Check(c);

        c.MustSubType(var_def->init->span, var_def->init->type, var_def->symbol->type);

        c.FinishExpr();
    }
}

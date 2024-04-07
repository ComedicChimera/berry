#include "checker.hpp"

std::unordered_set<std::string_view> valid_callconvs {
    "c", "win64", "stdcall"
};

void Checker::checkFuncAttrs(Decl* decl) {
    auto span = decl->ast_decl->an_Func.symbol->span;
    bool has_body = decl->ast_decl->an_Func.body != nullptr;

    bool is_extern = false, has_callconv = false;
    bool is_abientry = false, is_inline = false;
    for (auto& attr : decl->attrs) {
        if (attr.name == "extern") {
            if (has_body) {
                error(span, "@extern function cannot have a body");
            }

            is_extern = true;
        } else if (attr.name == "abientry") {
            is_abientry = true;
        } else if (attr.name == "callconv") {
            if (attr.value.size() == 0) {
                error(span, "@callconv requires an argument");
            } else if (!valid_callconvs.contains(attr.value)) {
                error(span, "unsupported calling convention: {}", attr.value);
            }

            has_callconv = true;
        } else if (attr.name == "inline") {
            if (attr.value.size() > 0) {
                error(span, "@inline cannot take an argument");
            }
        }
    }

    if (is_extern) {
        if (is_abientry) {
            error(span, "@abientry function cannot be marked @extern");
        } 
        
        if (is_inline) {
            error(span, "@inline function cannot be marked @extern");
        }

        return;
    } 
    
    if (has_callconv) {
        error(span, "@callconv can only be applied to external functions");
    } 
    
    if (!has_body) {
        error(span, "function must have a body");
    }
}

void Checker::checkGlobalVarAttrs(Decl* decl) {
    auto& avar = decl->ast_decl->an_Var;
    auto span = avar.symbol->span;
    bool is_abientry = false, is_extern = false;

    for (auto& attr : decl->attrs) {
        if (attr.value == "extern") {
            if (avar.init != nullptr) {
                error(span, "@extern global variable cannot have an initializer");
            } 

            if (avar.symbol->flags & SYM_COMPTIME) {
                error(span, "@extern cannot be applied to global constants");
            }

            is_extern = true;
        } else if (attr.value == "abientry") {
            if (avar.symbol->flags & SYM_COMPTIME) {
                error(span, "@abientry cannot be applied to global constants");
            }

            is_abientry = true;
        } else if (attr.value == "callconv") {
            error(span, "global variable cannot be marked @callconv");
        } else if (attr.value == "inline") {
            error(span, "global variable cannot be marked @inline");
        }
    }

    if (is_extern && is_abientry) {
        error(span, "global variable cannot be marked both @extern and @abientry");
    }
}
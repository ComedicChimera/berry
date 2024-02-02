#include "ast.hpp"
#include "checker.hpp"

void AstFuncDef::Check(Checker& c) {
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

void AstGlobalVarDef::Check(Checker& c) {
    if (var_def->init) {
        var_def->init->Check(c);

        c.MustSubType(var_def->init->span, var_def->init->type, var_def->symbol->type);

        c.FinishExpr();
    }
}

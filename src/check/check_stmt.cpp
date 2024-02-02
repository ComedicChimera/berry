#include "ast.hpp"
#include "checker.hpp"

void AstLocalVarDef::Check(Checker& c) {
    if (init != nullptr) {
        init->Check(c);

        if (symbol->type == nullptr) {
            symbol->type = init->type;
        } else {
            c.MustSubType(init->span, init->type, symbol->type);
        }

        c.FinishExpr();

        c.DeclareLocal(symbol);
    }
}
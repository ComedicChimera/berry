#include "ast.hpp"
#include "checker.hpp"

void AstBlock::Check(Checker& c) {
    for (auto& stmt : stmts) {
        stmt->Check(c);

        if (stmt->Flags() & ASTF_EXPR) {
            c.FinishExpr();    
        }
    }
}
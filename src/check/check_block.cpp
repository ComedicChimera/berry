#include "ast.hpp"
#include "checker.hpp"

void Checker::Visit(AstBlock& node) {
    for (auto& stmt : node.stmts) {
        visitNode(stmt);

        if (stmt->Flags() & ASTF_EXPR) {
            FinishExpr();    
        }
    }
}
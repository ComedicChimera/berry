#include "checker.hpp"

void Checker::Visit(AstLocalVarDef& node) {
    if (node.init != nullptr) {
        visitNode(node.init); 

        if (node.symbol->type == nullptr) {
            node.symbol->type = node.init->type;
        } else {
            MustSubType(node.init->span, node.init->type, node.symbol->type);
        }

        FinishExpr();

        DeclareLocal(node.symbol);
    }
}
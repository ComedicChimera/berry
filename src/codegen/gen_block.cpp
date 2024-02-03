#include "codegen.hpp"

void CodeGenerator::Visit(AstBlock& node) {
    for (auto& stmt : node.stmts) {
        visitNode(stmt);
    }
}
#include "codegen.hpp"

void CodeGenerator::Visit(AstLocalVarDef& node) {
    auto* prev_pos = builder.GetInsertBlock();

    builder.SetInsertPoint(var_block);
    auto* ll_var = builder.CreateAlloca(genType(node.symbol->type));
    node.symbol->llvm_value = ll_var;
    builder.SetInsertPoint(prev_pos);

    if (node.init != nullptr) {
        visitNode(node.init);

        builder.CreateStore(node.init->llvm_value, ll_var);
    }
}
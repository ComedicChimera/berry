#include "codegen.hpp"

void CodeGenerator::Visit(AstBlock& node) {
    for (auto& stmt : node.stmts) {
        visitNode(stmt);

        if (currentHasTerminator()) {
            break;
        }
    }
}

void CodeGenerator::Visit(AstCondBranch& node) {
    Panic("cond branch visited explicitly in codegen");
}

void CodeGenerator::Visit(AstIfTree& node) {
    auto* exit_block = appendBlock();

    llvm::BasicBlock* else_block;
    for (auto& branch : node.branches) {
        visitNode(branch.cond_expr);

        auto* then_block = appendBlock();
        else_block = appendBlock();
        builder.CreateCondBr(branch.cond_expr->llvm_value, then_block, else_block);

        setCurrentBlock(then_block);
        visitNode(branch.body);

        if (!currentHasTerminator()) {
            builder.CreateBr(exit_block);
        }

        setCurrentBlock(else_block);
    }

    if (node.else_body) {
        visitNode(node.else_body);

        if (!currentHasTerminator()) {
            builder.CreateBr(exit_block);
        }
    } else {
        builder.CreateBr(exit_block);
    }

    setCurrentBlock(exit_block);
    if (!hasPredecessor(exit_block)) {
        // The only way exit can have no predecessor is if we have an else block
        // that always jumps.
        deleteCurrentBlock(else_block);
    }
}

void CodeGenerator::Visit(AstWhileLoop& node) {
    auto* exit_block = appendBlock();

    auto* else_block = exit_block;
    if (node.else_clause) {
        else_block = appendBlock();

        auto* curr_block = getCurrentBlock();
        setCurrentBlock(else_block);
        visitNode(node.else_clause);

        if (!currentHasTerminator()) {
            builder.CreateBr(exit_block);
        }

        setCurrentBlock(curr_block);
    }

    if (node.is_do_while) {
        auto* body_block = appendBlock();
        auto* closer_block = appendBlock();

        builder.CreateBr(body_block);

        pushLoopContext(exit_block, closer_block);
        setCurrentBlock(body_block);
        visitNode(node.body);
        popLoopContext();

        if (!currentHasTerminator()) {
            builder.CreateBr(closer_block);
        }

        setCurrentBlock(closer_block);

        visitNode(node.cond_expr);
        builder.CreateCondBr(node.cond_expr->llvm_value, body_block, else_block);
    } else {
        auto* header_block = appendBlock();
        builder.CreateBr(header_block);
        
        setCurrentBlock(header_block);

        pushLoopContext(exit_block, header_block);
        visitNode(node.cond_expr);
        popLoopContext();

        auto* body_block = appendBlock();
        builder.CreateCondBr(node.cond_expr->llvm_value, body_block, else_block);

        setCurrentBlock(body_block);
        visitNode(node.body);

        if (!currentHasTerminator()) {
            builder.CreateBr(header_block);
        }
    }

    setCurrentBlock(exit_block);
    if (!hasPredecessor(exit_block)) {
        // The only way for exit_block to have no predecessors if for the loop
        // the have an else block which unconditionally jumps.
        deleteCurrentBlock(else_block);
    }
}

void CodeGenerator::Visit(AstForLoop& node) {
    if (node.var_def) {
        Visit(*node.var_def);
    }

    auto* exit_block = appendBlock();
    auto* else_block = exit_block;
    if (node.else_clause) {
        else_block = appendBlock();

        auto* curr_block = getCurrentBlock();
        setCurrentBlock(else_block);
        visitNode(node.else_clause);

        if (!currentHasTerminator()) {
            builder.CreateBr(exit_block);
        }

        setCurrentBlock(curr_block);
    }

    llvm::BasicBlock* header_block;
    llvm::BasicBlock* body_block;
    if (node.cond_expr) {
        header_block = appendBlock();
        builder.CreateBr(header_block);
        setCurrentBlock(header_block);

        visitNode(node.cond_expr);

        body_block = appendBlock();
        builder.CreateCondBr(node.cond_expr->llvm_value, body_block, else_block);
    } else {
        body_block = appendBlock();
        header_block = body_block;
    }

    llvm::BasicBlock* update_block;
    if (node.update_stmt) {
        update_block = appendBlock();

        setCurrentBlock(update_block);
        visitNode(node.update_stmt);
        builder.CreateBr(header_block);
    } else {
        update_block = header_block;
    }

    setCurrentBlock(body_block);

    pushLoopContext(exit_block, update_block);
    visitNode(node.body);
    popLoopContext();

    if (!currentHasTerminator()) {
        builder.CreateBr(update_block);
    }

    setCurrentBlock(exit_block);
    if (!hasPredecessor(exit_block)) {
        // See while loop implementation for why this is ok.
        deleteCurrentBlock(else_block);
    }
}

/* -------------------------------------------------------------------------- */

void CodeGenerator::Visit(AstLocalVarDef& node) {
    auto* prev_pos = getCurrentBlock();

    setCurrentBlock(var_block);
    auto* ll_var = builder.CreateAlloca(genType(node.symbol->type));
    node.symbol->llvm_value = ll_var;
    setCurrentBlock(prev_pos);

    debug.EmitLocalVariableInfo(node, ll_var);

    if (node.array_size > 0) {
        // TODO
    }

    if (node.init != nullptr) {
        visitNode(node.init);

        builder.CreateStore(node.init->llvm_value, ll_var);
    }
}

void CodeGenerator::Visit(AstAssign& node) {
    visitNode(node.rhs);

    pushValueMode(false);
    visitNode(node.lhs);
    popValueMode();

    if (node.assign_op_kind == AOP_NONE) {
        builder.CreateStore(node.rhs->llvm_value, node.lhs->llvm_value);
    } else {
        auto* ll_lhs_addr_val = node.lhs->llvm_value;

        AstBinaryOp binop(TextSpan{}, node.assign_op_kind, std::move(node.lhs), std::move(node.rhs));
        Visit(binop);

        builder.CreateStore(binop.llvm_value, ll_lhs_addr_val);
    }

    // TODO: debug value instrinsic
}

void CodeGenerator::Visit(AstIncDec& node) {
    pushValueMode(false);
    visitNode(node.lhs);
    popValueMode();

    auto* ll_lhs_addr_val = node.lhs->llvm_value;

    auto one_val = std::make_unique<AstIntLit>(TextSpan{}, node.lhs->type, 1);
    AstBinaryOp binop(TextSpan{}, node.op_kind, std::move(node.lhs), std::move(one_val));
    Visit(binop);

    builder.CreateStore(binop.llvm_value, ll_lhs_addr_val);

    // TODO: debug value intrinsic
}

void CodeGenerator::Visit(AstReturn& node) {
    if (node.value) {
        visitNode(node.value);
        builder.CreateRet(node.value->llvm_value);
    } else {
        builder.CreateRetVoid();
    }
}

void CodeGenerator::Visit(AstBreak& node) {
    builder.CreateBr(getLoopCtx().break_block);
}

void CodeGenerator::Visit(AstContinue& node) {
    builder.CreateBr(getLoopCtx().continue_block);
}


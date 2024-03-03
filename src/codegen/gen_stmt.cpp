#include "codegen.hpp"

void CodeGenerator::genStmt(AstStmt* node) {
    debug.SetDebugLocation(node->span);

    switch (node->kind) {
    case AST_BLOCK:
        for (auto& stmt : node->an_Block.stmts) {
            genStmt(stmt);

            if (currentHasTerminator()) {
                return;
            }
        }
        break;
    case AST_IF:
        genIfTree(node);
        break;
    case AST_WHILE:
        genWhileLoop(node);
        break;
    case AST_FOR:
        genForLoop(node);
        break;
    case AST_LOCAL_VAR:
        genLocalVar(node);
        break;
    case AST_ASSIGN:
        genAssign(node);
        break;
    case AST_INCDEC:
        genIncDec(node);
        break;
    case AST_EXPR_STMT:
        genExpr(node->an_ExprStmt.expr);
        break;
    case AST_RETURN:
        if (node->an_Return.value) {
            if (return_param) {
                genStoreExpr(node->an_Return.value, return_param);
            } else {
                irb.CreateRet(genExpr(node->an_Return.value));
            }
        } else {
            irb.CreateRetVoid();
        }
        break;
    case AST_BREAK:
        irb.CreateBr(getLoopCtx().break_block);
        break;
    case AST_CONTINUE:
        irb.CreateBr(getLoopCtx().continue_block);
        break;
    default:
        Panic("stmt codegen not implemented for {}", (int)node->kind);
    }
}

/* -------------------------------------------------------------------------- */

void CodeGenerator::genIfTree(AstStmt* node) {
    auto* exit_block = appendBlock();

    llvm::BasicBlock* else_block;
    for (auto& branch : node->an_If.branches) {
        auto* then_block = appendBlock();
        else_block = appendBlock();

        irb.CreateCondBr(genExpr(branch.cond_expr), then_block, else_block);

        setCurrentBlock(then_block);
        genStmt(branch.body);

        if (!currentHasTerminator()) {
            irb.CreateBr(exit_block);
        }

        setCurrentBlock(else_block);
    }

    if (node->an_If.else_block) {
        genStmt(node->an_If.else_block);

        if (!currentHasTerminator()) {
            irb.CreateBr(exit_block);
        }
    } else {
        irb.CreateBr(exit_block);
    }

    setCurrentBlock(exit_block);
    if (!hasPredecessor(exit_block)) {
        // The only way exit can have no predecessor is if we have an else block
        // that always jumps.
        deleteCurrentBlock(else_block);
    }
}

void CodeGenerator::genWhileLoop(AstStmt* node) {
    auto& awhile = node->an_While;

    auto* exit_block = appendBlock();

    auto* else_block = exit_block;
    if (awhile.else_block) {
        else_block = appendBlock();

        auto* curr_block = getCurrentBlock();
        setCurrentBlock(else_block);
        genStmt(awhile.else_block);

        if (!currentHasTerminator()) {
            irb.CreateBr(exit_block);
        }

        setCurrentBlock(curr_block);
    }

    if (awhile.is_do_while) {
        auto* body_block = appendBlock();
        auto* closer_block = appendBlock();

        irb.CreateBr(body_block);

        pushLoopContext(exit_block, closer_block);
        setCurrentBlock(body_block);
        genStmt(awhile.body);
        popLoopContext();

        if (!currentHasTerminator()) {
            irb.CreateBr(closer_block);
        }

        setCurrentBlock(closer_block);
        irb.CreateCondBr(genExpr(awhile.cond_expr), body_block, else_block);
    } else {
        auto* header_block = appendBlock();
        irb.CreateBr(header_block);
        
        setCurrentBlock(header_block);
        auto* body_block = appendBlock();
        irb.CreateCondBr(genExpr(awhile.cond_expr), body_block, else_block);

        setCurrentBlock(body_block);
        pushLoopContext(exit_block, header_block);
        genStmt(awhile.body);
        popLoopContext();

        if (!currentHasTerminator()) {
            irb.CreateBr(header_block);
        }
    }

    setCurrentBlock(exit_block);
    if (!hasPredecessor(exit_block)) {
        // The only way for exit_block to have no predecessors if for the loop
        // the have an else block which unconditionally jumps.
        deleteCurrentBlock(else_block);
    }
}

void CodeGenerator::genForLoop(AstStmt* node) {
    auto afor = node->an_For;

    if (afor.var_def) {
        genStmt(afor.var_def);
    }

    auto* exit_block = appendBlock();
    auto* else_block = exit_block;
    if (afor.else_block) {
        else_block = appendBlock();

        auto* curr_block = getCurrentBlock();
        setCurrentBlock(else_block);
        genStmt(afor.else_block);

        if (!currentHasTerminator()) {
            irb.CreateBr(exit_block);
        }

        setCurrentBlock(curr_block);
    }

    llvm::BasicBlock* header_block;
    llvm::BasicBlock* body_block;
    if (afor.cond_expr) {
        header_block = appendBlock();
        irb.CreateBr(header_block);
        setCurrentBlock(header_block);

        body_block = appendBlock();
        irb.CreateCondBr(genExpr(afor.cond_expr), body_block, else_block);
    } else {
        body_block = appendBlock();
        header_block = body_block;
    }

    llvm::BasicBlock* update_block;
    if (afor.update_stmt) {
        update_block = appendBlock();

        setCurrentBlock(update_block);
        genStmt(afor.update_stmt);
        irb.CreateBr(header_block);
    } else {
        update_block = header_block;
    }

    setCurrentBlock(body_block);

    pushLoopContext(exit_block, update_block);
    genStmt(afor.body);
    popLoopContext();

    if (!currentHasTerminator()) {
        irb.CreateBr(update_block);
    }

    setCurrentBlock(exit_block);
    if (!hasPredecessor(exit_block)) {
        // See while loop implementation for why this is ok.
        deleteCurrentBlock(else_block);
    }
}

/* -------------------------------------------------------------------------- */

void CodeGenerator::genLocalVar(AstStmt* node) {
    auto* ll_var = genStackAlloc(node->an_LocalVar.symbol->type);
    node->an_LocalVar.symbol->llvm_value = ll_var;

    debug.EmitLocalVariableInfo(node, ll_var);

    if (node->an_LocalVar.init != nullptr) {
        genStoreExpr(node->an_LocalVar.init, ll_var);
    }
}

void CodeGenerator::genAssign(AstStmt* node) {
    auto* lhs_addr = genExpr(node->an_Assign.lhs, true);

    if (node->an_Assign.assign_op == AOP_NONE) {
        genStoreExpr(node->an_Assign.rhs, lhs_addr);
    } else {
        AstExpr binop { };
        binop.span = node->span;
        binop.kind = AST_BINOP;
        binop.type = node->an_Assign.lhs->type;
        binop.an_Binop.op = node->an_Assign.assign_op;
        binop.an_Binop.lhs = node->an_Assign.lhs;
        binop.an_Binop.rhs = node->an_Assign.rhs;

        genStoreExpr(&binop, lhs_addr);
    }

    // TODO: debug value instrinsic
}

void CodeGenerator::genIncDec(AstStmt* node) {
    auto* lhs_addr = genExpr(node->an_IncDec.lhs, true);

    AstExpr one_val {};
    one_val.kind = AST_INT;
    one_val.span = node->span;
    one_val.type = node->an_IncDec.lhs->type;
    one_val.an_Int.value = 1;

    AstExpr binop {};
    binop.kind = AST_BINOP;
    binop.span = node->span;
    binop.type = node->an_IncDec.lhs->type;
    binop.an_Binop.lhs = node->an_IncDec.lhs;
    binop.an_Binop.rhs = &one_val;
    binop.an_Binop.op = node->an_IncDec.op;
    
    genStoreExpr(&binop, lhs_addr);
    // TODO: debug value intrinsic
}
#include "codegen.hpp"

void CodeGenerator::genStmt(AstStmt* node) {
    debug.SetDebugLocation(node->span);

    switch (node->kind) {
    case AST_BLOCK:
    case AST_UNSAFE:
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
    case AST_MATCH:
        genMatchStmt(node);
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
    case AST_FALLTHROUGH:
        if (fallthru_stack.empty())
            Panic("fallthrough outside of match context in codegen");

        irb.CreateBr(fallthru_stack.back());
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
    if (!hasPredecessor()) {
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
    if (!hasPredecessor()) {
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
    if (!hasPredecessor()) {
        // See while loop implementation for why this is ok.
        deleteCurrentBlock(else_block);
    }
}

void CodeGenerator::genMatchStmt(AstStmt* node) {
    std::vector<PatternBranch> branches;
    for (auto& acase : node->an_Match.cases) {
        auto* case_block = appendBlock();
        branches.emplace_back(acase.cond_expr, case_block);
    }

    auto* exit_block = appendBlock();

    genPatternMatch(node->an_Match.expr, branches, exit_block);

    for (size_t i = 0; i < branches.size(); i++) {
        auto& branch = branches[i];

        if (i == branches.size() - 1)
            fallthru_stack.push_back(exit_block);
        else
            fallthru_stack.push_back(branches[i+1].block);

        setCurrentBlock(branch.block);
        genStmt(node->an_Match.cases[i].body);

        if (!currentHasTerminator())
            irb.CreateBr(exit_block);

        fallthru_stack.pop_back();
    }

    setCurrentBlock(exit_block);
    if (!hasPredecessor()) {
        // All cases jump out: no exit block needed.
        deleteCurrentBlock(branches.back().block);
    } else if (node->an_Match.is_enum_exhaustive) {
        // Default case should never be reached!
        irb.CreateCall(rtstub_panic_unreachable);
        irb.CreateUnreachable();
    }
}

/* -------------------------------------------------------------------------- */

void CodeGenerator::genLocalVar(AstStmt* node) {
    auto& alocal = node->an_LocalVar;
    auto* symbol = alocal.symbol;

    if (symbol->flags & SYM_CONST) {
        ConstValue* const_value;
        if (alocal.init_expr)
            const_value = evalComptime(alocal.init_expr);
        else
            const_value = getComptimeNull(symbol->type);

        symbol->llvm_value = genComptime(const_value, CTG_NONE);
        return;
    }

    auto* ll_var = genAlloc(symbol->type, A_ALLOC_STACK);
    symbol->llvm_value = ll_var;

    debug.EmitLocalVariableInfo(node, ll_var);

    if (alocal.init_expr != nullptr) {
        genStoreExpr(alocal.init_expr, ll_var);
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
    one_val.an_Int.value = 1;

    if (node->an_IncDec.lhs->type->kind == TYPE_PTR) {
        // TODO: platform sized integers
        one_val.type = &prim_i64_type;
    } else {
        one_val.type = node->an_IncDec.lhs->type;
    }

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

/* -------------------------------------------------------------------------- */

CodeGenerator::LoopContext& CodeGenerator::getLoopCtx() {
    Assert(loop_ctx_stack.size() > 0, "loop control statement missing loop context in codegen");
    return loop_ctx_stack.back();
}

void CodeGenerator::pushLoopContext(llvm::BasicBlock* break_block, llvm::BasicBlock* continue_block) {
    loop_ctx_stack.emplace_back(break_block, continue_block);
}

void CodeGenerator::popLoopContext() {
    Assert(loop_ctx_stack.size() > 0, "pop on empty loop context stack in codegen");
    loop_ctx_stack.pop_back();
}
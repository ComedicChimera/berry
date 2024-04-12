#include "codegen.hpp"

void CodeGenerator::genStmt(HirStmt* node) {
    debug.SetDebugLocation(node->span);

    switch (node->kind) {
    case HIR_BLOCK:
    case HIR_UNSAFE:
        for (auto& stmt : node->ir_Block.stmts) {
            genStmt(stmt);

            if (currentHasTerminator()) {
                return;
            }
        }
        break;
    case HIR_IF:
        genIfTree(node);
        break;
    case HIR_WHILE:
    case HIR_DO_WHILE:
        genWhileLoop(node);
        break;
    case HIR_FOR:
        genForLoop(node);
        break;
    case HIR_MATCH:
        genMatchStmt(node);
        break;
    case HIR_LOCAL_VAR: {
        auto& hlocal = node->ir_LocalVar;
        auto* symbol = hlocal.symbol;

        auto* ll_var = genAlloc(symbol->type, HIRMEM_STACK);
        symbol->llvm_value = ll_var;

        debug.EmitLocalVariableInfo(node, ll_var);

        if (hlocal.init != nullptr) {
            genStoreExpr(hlocal.init, ll_var);
        }
    } break;
    case HIR_LOCAL_CONST: {
        auto& hlocal = node->ir_LocalConst;

        hlocal.symbol->llvm_value = genComptime(hlocal.init, CTG_CONST, hlocal.symbol->type);
    } break;
    case HIR_ASSIGN: {
        auto* lhs_addr = genExpr(node->ir_Assign.lhs, true);
        genStoreExpr(node->ir_Assign.rhs, lhs_addr);

        // TODO: debug value instrinsic
    } break;
    case HIR_CPD_ASSIGN:
        genCpdAssign(node);
        break;
    case HIR_INCDEC:
        genIncDec(node);
        break;
    case HIR_EXPR_STMT:
        genExpr(node->ir_ExprStmt.expr);
        break;
    case HIR_RETURN:
        if (node->ir_Return.expr) {
            if (return_param) {
                genStoreExpr(node->ir_Return.expr, return_param);
            } else {
                irb.CreateRet(genExpr(node->ir_Return.expr));
            }
        } else {
            irb.CreateRetVoid();
        }
        break;
    case HIR_BREAK:
        irb.CreateBr(getLoopCtx().break_block);
        break;
    case HIR_CONTINUE:
        irb.CreateBr(getLoopCtx().continue_block);
        break;
    case HIR_FALLTHRU:
        if (fallthru_stack.empty())
            Panic("fallthrough outside of match context in codegen");

        irb.CreateBr(fallthru_stack.back());
        break;
    default:
        Panic("stmt codegen not implemented for {}", (int)node->kind);
    }
}

/* -------------------------------------------------------------------------- */

void CodeGenerator::genIfTree(HirStmt* node) {
    auto* exit_block = appendBlock();

    llvm::BasicBlock* else_block;
    for (auto& branch : node->ir_If.branches) {
        auto* then_block = appendBlock();
        else_block = appendBlock();

        irb.CreateCondBr(genExpr(branch.cond), then_block, else_block);

        setCurrentBlock(then_block);
        genStmt(branch.body);

        if (!currentHasTerminator()) {
            irb.CreateBr(exit_block);
        }

        setCurrentBlock(else_block);
    }

    if (node->ir_If.else_stmt) {
        genStmt(node->ir_If.else_stmt);

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

void CodeGenerator::genWhileLoop(HirStmt* node) {
    auto& hwhile = node->ir_While;

    auto* exit_block = appendBlock();

    auto* else_block = exit_block;
    if (hwhile.else_stmt) {
        else_block = appendBlock();

        auto* curr_block = getCurrentBlock();
        setCurrentBlock(else_block);
        genStmt(hwhile.else_stmt);

        if (!currentHasTerminator()) {
            irb.CreateBr(exit_block);
        }

        setCurrentBlock(curr_block);
    }

    if (node->kind == HIR_DO_WHILE) {
        auto* body_block = appendBlock();
        auto* closer_block = appendBlock();

        irb.CreateBr(body_block);

        pushLoopContext(exit_block, closer_block);
        setCurrentBlock(body_block);
        genStmt(hwhile.body);
        popLoopContext();

        if (!currentHasTerminator()) {
            irb.CreateBr(closer_block);
        }

        setCurrentBlock(closer_block);
        irb.CreateCondBr(genExpr(hwhile.cond), body_block, else_block);
    } else {
        auto* header_block = appendBlock();
        irb.CreateBr(header_block);
        
        setCurrentBlock(header_block);
        auto* body_block = appendBlock();
        irb.CreateCondBr(genExpr(hwhile.cond), body_block, else_block);

        setCurrentBlock(body_block);
        pushLoopContext(exit_block, header_block);
        genStmt(hwhile.body);
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

void CodeGenerator::genForLoop(HirStmt* node) {
    auto hfor = node->ir_For;

    if (hfor.iter_var) {
        genStmt(hfor.iter_var);
    }

    auto* exit_block = appendBlock();
    auto* else_block = exit_block;
    if (hfor.else_stmt) {
        else_block = appendBlock();

        auto* curr_block = getCurrentBlock();
        setCurrentBlock(else_block);
        genStmt(hfor.else_stmt);

        if (!currentHasTerminator()) {
            irb.CreateBr(exit_block);
        }

        setCurrentBlock(curr_block);
    }

    llvm::BasicBlock* header_block;
    llvm::BasicBlock* body_block;
    if (hfor.cond) {
        header_block = appendBlock();
        irb.CreateBr(header_block);
        setCurrentBlock(header_block);

        body_block = appendBlock();
        irb.CreateCondBr(genExpr(hfor.cond), body_block, else_block);
    } else {
        body_block = appendBlock();
        header_block = body_block;
    }

    llvm::BasicBlock* update_block;
    if (hfor.update_stmt) {
        update_block = appendBlock();

        setCurrentBlock(update_block);
        genStmt(hfor.update_stmt);
        irb.CreateBr(header_block);
    } else {
        update_block = header_block;
    }

    setCurrentBlock(body_block);

    pushLoopContext(exit_block, update_block);
    genStmt(hfor.body);
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

void CodeGenerator::genMatchStmt(HirStmt* node) {
    std::vector<PatternBranch> branches;
    std::vector<std::pair<HirStmt*, llvm::BasicBlock*>> case_blocks;
    for (auto& hcase : node->ir_Match.cases) {
        auto* case_block = appendBlock();
        case_blocks.emplace_back(hcase.body, case_block);

        for (auto* pattern : hcase.patterns) {
            branches.emplace_back(pattern, case_block);
        }
    }

    auto* exit_block = appendBlock();

    genPatternMatch(node->ir_Match.expr, branches, exit_block);

    for (size_t i = 0; i < case_blocks.size(); i++) {
        auto [ body, block ] = case_blocks[i];

        if (i == case_blocks.size() - 1)
            fallthru_stack.push_back(exit_block);
        else
            fallthru_stack.push_back(case_blocks[i+1].second);

        setCurrentBlock(block);
        genStmt(body);

        if (!currentHasTerminator())
            irb.CreateBr(exit_block);

        fallthru_stack.pop_back();
    }

    setCurrentBlock(exit_block);
    if (!hasPredecessor()) {
        // All cases jump out: no exit block needed.
        deleteCurrentBlock(branches.back().block);
    } else if (node->ir_Match.is_implicit_exhaustive) {
        // Default case should never be reached!
        irb.CreateCall(rtstub_panic_unreachable);
        irb.CreateUnreachable();
    }
}

/* -------------------------------------------------------------------------- */

void CodeGenerator::genCpdAssign(HirStmt* node) {
    auto* lhs_addr = genExpr(node->ir_CpdAssign.lhs, true);

    HirExpr binop {};
    binop.span = node->span;
    binop.kind = HIR_BINOP;
    binop.type = node->ir_CpdAssign.binop_type;
    binop.ir_Binop.lhs = node->ir_CpdAssign.lhs;
    binop.ir_Binop.rhs = node->ir_CpdAssign.rhs;
    binop.ir_Binop.op = node->ir_CpdAssign.op;

    HirExpr* rhs_val = &binop;
    if (node->ir_CpdAssign.needs_subtype_cast) {
        HirExpr cast {};
        cast.span = node->span;
        cast.kind = HIR_CAST;
        cast.type = node->ir_CpdAssign.lhs->type;
        cast.ir_Cast.expr = rhs_val;
        rhs_val = &cast;
    }

    genStoreExpr(rhs_val, lhs_addr);

    // TODO: debug value instrinsic
}

void CodeGenerator::genIncDec(HirStmt* node) {
    auto* lhs_type = node->ir_IncDec.expr->type->Inner();
    auto* lhs_addr = genExpr(node->ir_IncDec.expr, true);

    HirExpr one_val {};
    one_val.kind = HIR_NUM_LIT;
    one_val.span = node->span;
    one_val.ir_Num.value = 1;

    if (lhs_type->kind == TYPE_PTR) {
        one_val.type = platform_uint_type;
    } else {
        one_val.type = lhs_type;
    }

    HirExpr binop {};
    binop.kind = HIR_BINOP;
    binop.span = node->span;
    binop.type = node->ir_IncDec.binop_type;
    binop.ir_Binop.lhs = node->ir_IncDec.expr;
    binop.ir_Binop.rhs = &one_val;
    binop.ir_Binop.op = node->ir_IncDec.op;

    HirExpr* rhs_val = &binop;
    if (node->ir_IncDec.needs_subtype_cast) {
        HirExpr cast {};
        cast.kind = HIR_CAST;
        cast.span = node->span;
        cast.type = lhs_type;
        cast.ir_Cast.expr = rhs_val;
        rhs_val = &cast;
    }
    
    genStoreExpr(rhs_val, lhs_addr);

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
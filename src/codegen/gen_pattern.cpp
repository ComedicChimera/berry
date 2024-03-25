#include "codegen.hpp"

static bool alwaysMatches(AstExpr* pattern) {
    switch (pattern->kind) {
    case AST_PATTERN_LIST:
        for (auto* sub_pattern : pattern->an_PatternList.patterns) {
            if (alwaysMatches(sub_pattern))
                return true;
        }
        break;
    case AST_IDENT:
        return true;
    }

    return false;
}

void CodeGenerator::genCasePatternMatch(AstExpr* expr, const std::vector<PatternBranch>& pcases, llvm::BasicBlock* nm_block) {
    auto* match_operand = genExpr(expr);

    auto* expr_type = expr->type->Inner();
    if (expr_type->kind == TYPE_NAMED)
        expr_type = expr_type->ty_Named.type;

    switch (expr_type->kind) {
    case TYPE_INT: case TYPE_ENUM: case TYPE_BOOL: {
        auto* ll_switch = irb.CreateSwitch(
            match_operand,
            nm_block,
            pcases.size()   
        );

        for (auto& pcase : pcases) {
            if (pcase.pattern->kind == AST_PATTERN_LIST) {
                if (alwaysMatches(pcase.pattern)) {
                    ll_switch->setDefaultDest(pcase.block);
                    return;
                }

                for (auto* sub_pattern : pcase.pattern->an_PatternList.patterns) {
                    pmGenAtomWithSwitch(ll_switch, sub_pattern, pcase.block);
                }
            } else if (pmGenAtomWithSwitch(ll_switch, pcase.pattern, pcase.block)) {
                return;
            }
        }
    } break;
    default:
        for (auto& pcase : pcases) {
            if (pcase.pattern->kind == AST_PATTERN_LIST) {
                if (alwaysMatches(pcase.pattern)) {
                    irb.CreateBr(pcase.block);
                    return;
                }

                for (auto* sub_pattern : pcase.pattern->an_PatternList.patterns) {
                    pmGenAtomWithEq(match_operand, sub_pattern, pcase.block);
                }
            } else if (pmGenAtomWithEq(match_operand, pcase.pattern, pcase.block)) {
                return;
            }
        }

        irb.CreateBr(nm_block); 
        break;
    }
}

bool CodeGenerator::pmGenAtomWithSwitch(llvm::SwitchInst* ll_switch, AstExpr* pattern, llvm::BasicBlock* case_block) {
    switch (pattern->kind) {
    case AST_INT:
        ll_switch->addCase(
            llvm::dyn_cast<llvm::ConstantInt>(makeLLVMIntLit(pattern->type, pattern->an_Int.value)), 
            case_block
        );
        break;
    case AST_ENUM_LIT: {
        auto* tag_value = pattern->type->FullUnwrap()->ty_Enum.tag_values[pattern->an_Field.field_index];

        ll_switch->addCase(
            llvm::dyn_cast<llvm::ConstantInt>(tag_value),
            case_block
        );
    } break;
    case AST_IDENT:
        ll_switch->setDefaultDest(case_block);

        if (pattern->an_Ident.symbol != nullptr) {
            pmGenCapture(ll_switch->getCondition(), pattern->an_Ident.symbol, case_block);
        }

        return true;
    default:
        Panic("switch pattern matching not implemented for {}", (int)pattern->kind);
        break;
    }

    return false;
}

bool CodeGenerator::pmGenAtomWithEq(llvm::Value* match_operand, AstExpr* pattern, llvm::BasicBlock* case_block) {  
    if (pattern->kind == AST_IDENT) {
        irb.CreateBr(case_block);

        if (pattern->an_Ident.symbol != nullptr) {
            pmGenCapture(match_operand, pattern->an_Ident.symbol, case_block);
        }

        return true;
    }

    auto* fail_block = appendBlock();

    switch (pattern->kind) {
    case AST_FLOAT: {
        auto* cmp_result = irb.CreateFCmpUEQ(match_operand, makeLLVMFloatLit(pattern->type, pattern->an_Float.value));
        irb.CreateCondBr(cmp_result, case_block, fail_block);
    } break;
    default:
        Panic("eq pattern matching not implemented for {}", (int)pattern->kind);
        break;    
    }

    setCurrentBlock(fail_block);
    return false;
}

void CodeGenerator::pmGenCapture(llvm::Value* match_operand, Symbol* capture_sym, llvm::BasicBlock* case_block) {
    auto* prev_block = getCurrentBlock();
    setCurrentBlock(case_block);

    auto* ll_capture_type = genType(capture_sym->type, true);
    auto* capture = genAlloc(ll_capture_type, A_ALLOC_STACK);
    if (shouldPtrWrap(match_operand->getType())) {
        genStructCopy(ll_capture_type, match_operand, capture);
    } else {
        irb.CreateStore(match_operand, capture);
    }

    capture_sym->llvm_value = capture;

    setCurrentBlock(prev_block);
}
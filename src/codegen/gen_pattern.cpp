#include "codegen.hpp"

void CodeGenerator::genPatternMatch(AstExpr* expr, const std::vector<PatternBranch>& pcases, llvm::BasicBlock* nm_block) {    
    auto* expr_type = expr->type->FullUnwrap();
    auto* match_operand = genExpr(expr);

    llvm::SwitchInst* pswitch { nullptr };
    switch (expr_type->kind) {
    case TYPE_STRING:
        pmGenStrMatch(match_operand, pcases, nm_block);
        return;
    case TYPE_INT: case TYPE_BOOL: case TYPE_ENUM:
        pswitch = irb.CreateSwitch(match_operand, nm_block, pcases.size());
        break;
    }

    for (auto& pcase : pcases) {
        if (pmAddCase(pswitch, match_operand, pcase.pattern, pcase.block)) {
            break;
        }
    }

    if (!currentHasTerminator()) {
        irb.CreateBr(nm_block);
    }
}

bool CodeGenerator::pmAddCase(llvm::SwitchInst* pswitch, llvm::Value* match_operand, AstExpr* pattern, llvm::BasicBlock* case_block) {
    switch (pattern->kind) {
    case AST_INT:
        pswitch->addCase(
            llvm::dyn_cast<llvm::ConstantInt>(makeLLVMIntLit(pattern->type, pattern->an_Int.value)), 
            case_block
        );

        return false;
    case AST_FLOAT: {
        auto* fail_block = appendBlock();

        auto* cmp_result = irb.CreateFCmpUEQ(match_operand, makeLLVMFloatLit(pattern->type, pattern->an_Float.value));
        irb.CreateCondBr(cmp_result, case_block, fail_block);
        setCurrentBlock(fail_block);

        return false;
    } break;
    case AST_ENUM_LIT: {
        auto* tag_value = pattern->type->FullUnwrap()->ty_Enum.tag_values[pattern->an_Field.field_index];

        pswitch->addCase(
            llvm::dyn_cast<llvm::ConstantInt>(tag_value),
            case_block
        );

        return false;
    } break;
    case AST_IDENT: 
        if (pswitch == nullptr) {
            irb.CreateBr(case_block);
        } else {
            pswitch->setDefaultDest(case_block);
        }

        if (pattern->an_Ident.symbol != nullptr) {
            pmGenCapture(pattern->an_Ident.symbol, match_operand, case_block);
        }

        return true;
    case AST_PATTERN_LIST: 
        for (auto* sub_pattern : pattern->an_PatternList.patterns) {
            if (pmAddCase(pswitch, match_operand, sub_pattern, case_block)) {
                return true;
            }
        }

        return false;
    default:
        Panic("pattern matching not implemented for node {}", (int)pattern->kind);
        return false;
    }
}

void CodeGenerator::pmGenCapture(Symbol* capture_sym, llvm::Value* match_operand, llvm::BasicBlock* case_block) {
    auto* prev_block = getCurrentBlock();
    setCurrentBlock(case_block);

    auto* ll_capture_type = genType(capture_sym->type, true);
    auto* capture = genAlloc(ll_capture_type, A_ALLOC_STACK);
    if (shouldPtrWrap(match_operand->getType())) {
        genMemCopy(ll_capture_type, match_operand, capture);
    } else {
        irb.CreateStore(match_operand, capture);
    }

    capture_sym->llvm_value = capture;

    setCurrentBlock(prev_block);
}

/* -------------------------------------------------------------------------- */

void CodeGenerator::pmGenStrMatch(llvm::Value* match_operand, const std::vector<PatternBranch>& pcases, llvm::BasicBlock* nm_block) {
    PatternBuckets buckets;
    auto* default_block = nm_block;
    for (auto& pcase : pcases) {
        auto* new_default_block = pmAddStringCase(buckets, match_operand, pcase.pattern, pcase.block);
        if (new_default_block) {
            default_block = new_default_block;
            break;
        }
    }

    auto* ll_hash_value = irb.CreateCall(rtstub_strhash, match_operand);
    auto* pswitch = irb.CreateSwitch(ll_hash_value, default_block, pcases.size());

    for (auto& pair : buckets) {
        auto* bucket_block = appendBlock();
        setCurrentBlock(bucket_block);

        pswitch->addCase(
            llvm::dyn_cast<llvm::ConstantInt>(getPlatformIntConst(pair.first)),
            bucket_block
        );

        for (auto& bucket_entry : pair.second) {
            auto* fail_block = appendBlock();

            auto* eq_result = genStrEq(match_operand, genStrLit(bucket_entry.pattern, nullptr));
            irb.CreateCondBr(eq_result, bucket_entry.block, fail_block);

            setCurrentBlock(fail_block);
        }

        irb.CreateBr(default_block);
    }

    return;
}

#define FNV_OFFSET 0xcbf29ce484222325
#define FNV_PRIME 0x100000001b3

static size_t berryStrHash(std::string_view str) {
    // NOTE: This hash function must behave *identically* to `strhash` as
    // implemented in runtime/strmem.bry.  If there is any difference, then
    // string pattern matching will break (very not good).

    size_t h = FNV_OFFSET;
    for (char c : str) {
        h ^= (size_t)c;
        h *= FNV_PRIME;
    }

    return h;
}


llvm::BasicBlock* CodeGenerator::pmAddStringCase(PatternBuckets& buckets, llvm::Value* match_operand, AstExpr* pattern, llvm::BasicBlock* case_block) {
    switch (pattern->kind) {
    case AST_IDENT:
        if (pattern->an_Ident.symbol) {
            pmGenCapture(pattern->an_Ident.symbol, match_operand, case_block);
        }

        return case_block;
    case AST_PATTERN_LIST:
        for (auto* sub_pattern : pattern->an_PatternList.patterns) {
            auto* new_default_block = pmAddStringCase(buckets, match_operand, sub_pattern, case_block);
            if (new_default_block) {
                return new_default_block;
            }
        }

        return nullptr;
    case AST_STRING: {
        auto hash_value = berryStrHash(pattern->an_String.value);

        auto it = buckets.find(hash_value);
        if (it == buckets.end()) {
            std::vector<PatternBranch> bucket{ { pattern, case_block } };
            buckets.emplace(hash_value, std::move(bucket));
        } else {
            it->second.emplace_back(pattern, case_block);
        }

        return nullptr;
    } break;
    default:
        Panic("pattern matching not implemented for node {}", (int)pattern->kind);
        return nullptr;
    }

}

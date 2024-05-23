#include "codegen.hpp"

llvm::Value* CodeGenerator::genExpr(HirExpr* node, bool expect_addr, llvm::Value* alloc_loc) {
    debug.SetDebugLocation(node->span);

    switch (node->kind) {
    case HIR_TEST_MATCH:
        return genTestMatch(node);
    case HIR_CAST:
        return genCast(node);
    case HIR_BINOP:
        return genBinop(node);
    case HIR_UNOP:
        return genUnop(node);
    case HIR_ADDR:
        Assert(node->ir_Addr.expr->assignable, "tried to take address of an unassignable value in codegen");
        return genExpr(node->ir_Addr.expr, true);
    case HIR_DEREF: {
        auto ptr_val = genExpr(node->ir_Deref.expr);
    
        if (expect_addr || shouldPtrWrap(node->type)) {
            return ptr_val;
        } else {
            return irb.CreateLoad(genType(node->type), ptr_val);
        }
    } break;
    case HIR_CALL:
        return genCall(node, alloc_loc);
    case HIR_CALL_METHOD:
        return genCallMethod(node, alloc_loc);
    case HIR_CALL_FACTORY:
        return genCallFactory(node, alloc_loc);
    case HIR_INDEX:
        return genIndexExpr(node, expect_addr);
    case HIR_SLICE:
        return genSliceExpr(node, alloc_loc);
    case HIR_FIELD:
    case HIR_DEREF_FIELD:
        return genFieldExpr(node, expect_addr);
    case HIR_STATIC_GET: {
        auto* imported_symbol = node->ir_StaticGet.imported_symbol;
        auto* ll_value = loaded_imports[node->ir_StaticGet.dep_id][imported_symbol->decl_number];

        if (!expect_addr && (imported_symbol->flags & SYM_VAR) && !shouldPtrWrap(node->type)) {
            return irb.CreateLoad(genType(node->type), ll_value);
        }

        return ll_value;
    } break;
    case HIR_NEW:
        return genNewExpr(node);
    case HIR_NEW_ARRAY:
        return genNewArray(node, alloc_loc);
    case HIR_NEW_STRUCT:
        return genNewStruct(node);
    case HIR_ARRAY_LIT:
        return genArrayLit(node, alloc_loc);
    case HIR_STRUCT_LIT:
        return genStructLit(node, alloc_loc);
    case HIR_ENUM_LIT: 
        return getPlatformIntConst(node->ir_EnumLit.tag_value);
    case HIR_UNSAFE_EXPR:
        return genExpr(node->ir_UnsafeExpr.expr, expect_addr, alloc_loc);
    case HIR_IDENT: 
        return genIdent(node, expect_addr);
    case HIR_NUM_LIT: {
        // NOTE: It is possible for an number literal to have a float type if a
        // number literal implicitly takes on a floating point value.
        auto inner_type = node->type->FullUnwrap();
        switch (inner_type->kind) {
        case TYPE_INT: 
            return makeLLVMIntLit(inner_type, node->ir_Num.value);
        case TYPE_FLOAT: 
            return makeLLVMFloatLit(inner_type, (double)node->ir_Num.value);
        case TYPE_ENUM:
            return getPlatformIntConst(node->ir_Num.value);
        case TYPE_PTR: {
            auto* int_lit = makeLLVMIntLit(platform_uint_type, node->ir_Num.value);
            return irb.CreateIntToPtr(int_lit, llvm::PointerType::get(ctx, 0));
        } break;
        default:
            Panic("non-numeric type integer literal in codegen");
        }    
    } break;
    case HIR_FLOAT_LIT: {
        return makeLLVMFloatLit(node->type->Inner(), node->ir_Float.value);
    } break;
    case HIR_BOOL_LIT:
        return makeLLVMIntLit(node->type, (uint64_t)node->ir_Bool.value);
    case HIR_NULL:
        return getNullValue(genType(node->type));
    case HIR_STRING_LIT:
        return genStringLit(node, alloc_loc);
    case HIR_MACRO_SIZEOF:
        return makeLLVMIntLit(platform_uint_type, getLLVMByteSize(genType(node->ir_TypeMacro.arg, true)));
    case HIR_MACRO_ALIGNOF:
        return makeLLVMIntLit(platform_uint_type, getLLVMByteAlign(genType(node->ir_TypeMacro.arg, true)));
    default:
        Panic("expr codegen not implemented for {}", (int)node->kind);
        break;
    }

    return nullptr;
}

/* -------------------------------------------------------------------------- */

llvm::Value* CodeGenerator::genTestMatch(HirExpr* node) {
    auto* true_block = appendBlock();
    auto* false_block = appendBlock();
    auto* end_block = appendBlock();

    std::vector<PatternBranch> pcases;
    for (auto* pattern : node->ir_TestMatch.patterns) {
        pcases.emplace_back(pattern, true_block);
    }

    genPatternMatch(node->ir_TestMatch.expr, pcases, false_block);

    setCurrentBlock(true_block);
    irb.CreateBr(end_block);

    setCurrentBlock(false_block);
    if (hasPredecessor()) {
        irb.CreateBr(end_block);
        setCurrentBlock(end_block);
    } else {
        deleteCurrentBlock(end_block);
        return makeLLVMIntLit(&prim_bool_type, 1);
    }

    auto* phi_node = irb.CreatePHI(llvm::Type::getInt1Ty(ctx), 2);
    phi_node->addIncoming(makeLLVMIntLit(&prim_bool_type, 1), true_block);
    phi_node->addIncoming(makeLLVMIntLit(&prim_bool_type, 0), false_block);

    return phi_node;
}

/* -------------------------------------------------------------------------- */

llvm::Value* CodeGenerator::genCast(HirExpr* node) {
    auto& hcast = node->ir_Cast;

    auto* src_val = genExpr(hcast.expr);
    if (tctx.Equal(hcast.expr->type, node->type)) {
        return src_val;
    }

    auto* src_type = hcast.expr->type->FullUnwrap();
    auto* dest_type = node->type->FullUnwrap();

    auto src_kind = src_type->kind;
    auto dest_kind = dest_type->kind;

    auto* ll_dtype = genType(dest_type);
    switch (dest_kind) {
    case TYPE_INT: {
        if (src_kind == TYPE_INT) {
            return irb.CreateIntCast(src_val, ll_dtype, src_type->ty_Int.is_signed);
        } else if (src_kind == TYPE_FLOAT) {
            if (dest_type->ty_Int.is_signed) {
                return irb.CreateFPToSI(src_val, ll_dtype);
            } else {
                return irb.CreateFPToUI(src_val, ll_dtype);
            }
        } else if (src_kind == TYPE_BOOL) {
            return irb.CreateZExt(src_val, ll_dtype);
        } else if (src_kind == TYPE_PTR) {
            return irb.CreatePtrToInt(src_val, ll_dtype);
        } else if (src_kind == TYPE_ENUM) {
            return irb.CreateIntCast(src_val, ll_dtype, false);
        }
    } break; 
    case TYPE_FLOAT:
        if (src_kind == TYPE_INT) {
            if (src_type->ty_Int.is_signed) {
                return irb.CreateSIToFP(src_val, ll_dtype);
            } else {
                return irb.CreateUIToFP(src_val, ll_dtype);
            }
        } else if (src_kind == TYPE_FLOAT) {
            return irb.CreateFPCast(src_val, ll_dtype);
        }
        break;
    case TYPE_BOOL:
        return irb.CreateTrunc(src_val, ll_dtype);
    case TYPE_PTR:
        if (src_kind == TYPE_PTR) {
            return src_val;
        } else if (src_kind == TYPE_INT) {
            return irb.CreateIntToPtr(src_val, ll_dtype);
        }
        break;
    case TYPE_ARRAY:
        if (src_kind == TYPE_SLICE || src_kind == TYPE_STRING) {
            return getSliceData(src_val);
        }
        break;
    case TYPE_SLICE:
        if (src_kind == TYPE_STRING) {
            return src_val;
        } else if (src_kind == TYPE_ARRAY) {
            llvm::Value* slice_val = getNullValue(ll_slice_type);
            slice_val = irb.CreateInsertValue(slice_val, src_val, 0);
            slice_val = irb.CreateInsertValue(slice_val, getPlatformIntConst(src_type->ty_Array.len), 1);
            return slice_val;
        }
        break;
    case TYPE_STRING:
        if (src_kind == TYPE_SLICE) {
            return src_val;
        } else if (src_kind == TYPE_ARRAY) {
            llvm::Value* string_val = getNullValue(ll_slice_type);
            string_val = irb.CreateInsertValue(string_val, src_val, 0);
            string_val = irb.CreateInsertValue(string_val, getPlatformIntConst(src_type->ty_Array.len), 1);
            return string_val;
        }
        break;
    case TYPE_ENUM:
        if (src_kind == TYPE_INT) {
            return irb.CreateIntCast(src_val, ll_dtype, src_type->ty_Int.is_signed);
        }            
        break;
    
    }

    Panic("unimplemented cast in codegen");
    return nullptr;
}

llvm::Value* CodeGenerator::genBinop(HirExpr* node) {
    auto lhs_val = genExpr(node->ir_Binop.lhs);

    // Handle short circuit operators.
    if (node->ir_Binop.op == HIROP_LGAND) {
        auto* start_block = getCurrentBlock(); 
        auto* true_block = appendBlock();
        auto* end_block = appendBlock();

        irb.CreateCondBr(lhs_val, true_block, end_block);

        setCurrentBlock(true_block);
        auto* rhs_val = genExpr(node->ir_Binop.rhs);
        irb.CreateBr(end_block);

        setCurrentBlock(end_block);
        auto* phi_node = irb.CreatePHI(llvm::Type::getInt1Ty(ctx), 2);
        phi_node->addIncoming(lhs_val, start_block);
        phi_node->addIncoming(rhs_val, true_block);
        return phi_node;
    } else if (node->ir_Binop.op == HIROP_LGOR) {
        auto* start_block = getCurrentBlock();
        auto* false_block = appendBlock();
        auto* end_block = appendBlock();

        irb.CreateCondBr(lhs_val, end_block, false_block);

        setCurrentBlock(false_block);
        auto* rhs_val = genExpr(node->ir_Binop.rhs);
        irb.CreateBr(end_block);

        setCurrentBlock(end_block);
        auto* phi_node = irb.CreatePHI(llvm::Type::getInt1Ty(ctx), 2);
        phi_node->addIncoming(lhs_val, start_block);
        phi_node->addIncoming(rhs_val, false_block);
        return phi_node;
    }

    auto* lhs_type = node->ir_Binop.lhs->type->Inner();
    auto* rhs_type = node->ir_Binop.rhs->type->Inner();
    auto* rhs_val = genExpr(node->ir_Binop.rhs);
    switch (node->ir_Binop.op) {
    case HIROP_ADD:
        if (lhs_type->kind == TYPE_PTR) {
            return irb.CreateGEP(genType(lhs_type->ty_Ptr.elem_type, true), lhs_val, { rhs_val });
        } else if (lhs_type->kind == TYPE_INT) {
            if (rhs_type->kind == TYPE_PTR) {
                return irb.CreateGEP(genType(rhs_type->ty_Ptr.elem_type, true), rhs_val, { lhs_val });
            } else {
                return irb.CreateAdd(lhs_val, rhs_val);
            }
        } else {
            Assert(lhs_type->kind == TYPE_FLOAT, "invalid types for ADD op in codegen");
            return irb.CreateFAdd(lhs_val, rhs_val);
        }
        break;
    case HIROP_SUB:
        if (lhs_type->kind == TYPE_PTR) {
            if (rhs_type->kind == TYPE_PTR) {
                return irb.CreatePtrDiff(genType(lhs_type->ty_Ptr.elem_type, true), lhs_val, rhs_val);
            } else {
                rhs_val = irb.CreateNeg(rhs_val);
                return irb.CreateGEP(genType(lhs_type->ty_Ptr.elem_type, true), lhs_val, { rhs_val });
            }
        } else if (lhs_type->kind == TYPE_INT) {
            if (rhs_type->kind == TYPE_PTR) {
                lhs_val = irb.CreateNeg(lhs_val);
                return irb.CreateGEP(genType(rhs_type->ty_Ptr.elem_type, true), rhs_val, { lhs_val });
            } else {
                return irb.CreateSub(lhs_val, rhs_val);
            }
        }else {
            Assert(lhs_type->kind == TYPE_FLOAT, "invalid types for SUB op in codegen");
            return irb.CreateFSub(lhs_val, rhs_val);
        }
        break;
    case HIROP_MUL:
        if (lhs_type->kind == TYPE_INT) {
            return irb.CreateMul(lhs_val, rhs_val);
        } else {
            Assert(lhs_type->kind == TYPE_FLOAT, "invalid types for MUL op in codegen");
            return irb.CreateFMul(lhs_val, rhs_val);
        }
        break;
    case HIROP_DIV:
        if (lhs_type->kind == TYPE_INT) {
            genDivideByZeroCheck(rhs_val, lhs_type);

            if (lhs_type->ty_Int.is_signed) {
                genDivideOverflowCheck(lhs_val, rhs_val, lhs_type);
                return irb.CreateSDiv(lhs_val, rhs_val);
            } else {
                return irb.CreateUDiv(lhs_val, rhs_val);
            }
        } else {
            Assert(lhs_type->kind == TYPE_FLOAT, "invalid types for DIV op in codegen");
            return irb.CreateFDiv(lhs_val, rhs_val);         
        }
        break;
    case HIROP_MOD:
        if (lhs_type->kind == TYPE_INT) {
            genDivideByZeroCheck(rhs_val, lhs_type);

            if (lhs_type->ty_Int.is_signed) {
                return irb.CreateSRem(lhs_val, rhs_val);
            } else {
                return irb.CreateURem(lhs_val, rhs_val);
            }
        } else {
            Assert(lhs_type->kind == TYPE_FLOAT, "invalid types for MOD op in codegen");
            return irb.CreateFRem(lhs_val, rhs_val);
        }
        break;
    case HIROP_SHL:
        if (lhs_type->kind == TYPE_PTR) {
            lhs_val = irb.CreatePtrToInt(lhs_val, ll_platform_int_type);
            rhs_val = irb.CreateIntCast(rhs_val, ll_platform_int_type, false);

            genShiftOverflowCheck(rhs_val, platform_int_type);
            auto* result = irb.CreateShl(lhs_val, rhs_val);
            return irb.CreatePtrToInt(result, llvm::PointerType::get(ctx, 0));
        } else {
            Assert(lhs_type->kind == TYPE_INT, "invalid types for SHL op in codegen");
            genShiftOverflowCheck(rhs_val, lhs_type);
            return irb.CreateShl(lhs_val, rhs_val);
        }
        break;
    case HIROP_SHR:
        if (lhs_type->kind == TYPE_PTR) {
            lhs_val = irb.CreatePtrToInt(lhs_val, ll_platform_int_type);
            rhs_val = irb.CreateIntCast(rhs_val, ll_platform_int_type, false);

            genShiftOverflowCheck(rhs_val, platform_int_type);
            auto* result = irb.CreateLShr(lhs_val, rhs_val);
            return irb.CreatePtrToInt(result, llvm::PointerType::get(ctx, 0));
        } else {
            Assert(lhs_type->kind == TYPE_INT, "invalid types for SHR op in codegen");

            genShiftOverflowCheck(rhs_val, lhs_type);
            if (lhs_type->ty_Int.is_signed) {
                return irb.CreateAShr(lhs_val, rhs_val);
            } else {
                return irb.CreateLShr(lhs_val, rhs_val);
            }
        } 
        break;
    case HIROP_EQ:
        lhs_type = lhs_type->FullUnwrap();
        switch (lhs_type->kind) {
        case TYPE_INT:
        case TYPE_BOOL:
        case TYPE_PTR:
        case TYPE_ENUM:
            return irb.CreateICmpEQ(lhs_val, rhs_val);
        case TYPE_FLOAT:
            return irb.CreateFCmpOEQ(lhs_val, rhs_val);
        case TYPE_STRING:
            return genStrEq(lhs_val, rhs_val);
        default:
            Panic("invalid types for EQ op in codegen");
            return nullptr;
        }
        break;
    case HIROP_NE:
        lhs_type = lhs_type->FullUnwrap();
        switch (lhs_type->kind) {
        case TYPE_INT:
        case TYPE_BOOL:
        case TYPE_PTR:
        case TYPE_ENUM:
            return irb.CreateICmpNE(lhs_val, rhs_val);
        case TYPE_FLOAT:
            return irb.CreateFCmpONE(lhs_val, rhs_val);
        case TYPE_STRING:
            return irb.CreateNot(genStrEq(lhs_val, rhs_val));
        default:
            Panic("invalid types for NE op in codegen");
            return nullptr;
        }
        break;
    case HIROP_LT:
        if (lhs_type->kind == TYPE_PTR) {
            if (rhs_type->kind == TYPE_INT) {
                rhs_val = irb.CreateIntToPtr(rhs_val, llvm::PointerType::get(ctx, 0));
            }

            return irb.CreateICmpULT(lhs_val, rhs_val);
        } else if (lhs_type->kind == TYPE_INT) {
            if (rhs_type->kind == TYPE_PTR) {
                lhs_val = irb.CreateIntToPtr(lhs_val, llvm::PointerType::get(ctx, 0));

                return irb.CreateICmpULT(lhs_val, rhs_val);
            } else if (lhs_type->ty_Int.is_signed) {
                return irb.CreateICmpSLT(lhs_val, rhs_val);
            } else {
                return irb.CreateICmpULT(lhs_val, rhs_val);
            }
        } else {
            Assert(lhs_type->kind == TYPE_FLOAT, "invalid types for LT op in codegen");
            return irb.CreateFCmpOLT(lhs_val, rhs_val);
        }
        break;
    case HIROP_GT:
        if (lhs_type->kind == TYPE_PTR) {
            if (rhs_type->kind == TYPE_INT) {
                rhs_val = irb.CreateIntToPtr(rhs_val, llvm::PointerType::get(ctx, 0));
            }

            return irb.CreateICmpUGT(lhs_val, rhs_val);
        } else if (lhs_type->kind == TYPE_INT) {
            if (rhs_type->kind == TYPE_PTR) {
                lhs_val = irb.CreateIntToPtr(lhs_val, llvm::PointerType::get(ctx, 0));

                return irb.CreateICmpUGT(lhs_val, rhs_val);
            } else if (lhs_type->ty_Int.is_signed) {
                return irb.CreateICmpSGT(lhs_val, rhs_val);
            } else {
                return irb.CreateICmpUGT(lhs_val, rhs_val);
            }
        } else {
            Assert(lhs_type->kind == TYPE_FLOAT, "invalid types for GT op in codegen");
            return irb.CreateFCmpOGT(lhs_val, rhs_val);
        }
        break;
    case HIROP_LE:
        if (lhs_type->kind == TYPE_PTR) {
            if (rhs_type->kind == TYPE_INT) {
                rhs_val = irb.CreateIntToPtr(rhs_val, llvm::PointerType::get(ctx, 0));
            }

            return irb.CreateICmpULE(lhs_val, rhs_val);
        } else if (lhs_type->kind == TYPE_INT) {
            if (rhs_type->kind == TYPE_PTR) {
                lhs_val = irb.CreateIntToPtr(lhs_val, llvm::PointerType::get(ctx, 0));

                return irb.CreateICmpULE(lhs_val, rhs_val);
            } else if (lhs_type->ty_Int.is_signed) {
                return irb.CreateICmpSLE(lhs_val, rhs_val);
            } else {
                return irb.CreateICmpULE(lhs_val, rhs_val);
            }
        } else {
            Assert(lhs_type->kind == TYPE_FLOAT, "invalid types for LE op in codegen");
            return irb.CreateFCmpOLE(lhs_val, rhs_val);
        }
        break;
    case HIROP_GE:
        if (lhs_type->kind == TYPE_PTR) {
            if (rhs_type->kind == TYPE_INT) {
                rhs_val = irb.CreateIntToPtr(rhs_val, llvm::PointerType::get(ctx, 0));
            }

            return irb.CreateICmpUGE(lhs_val, rhs_val);
        } else if (lhs_type->kind == TYPE_INT) {
            if (rhs_type->kind == TYPE_PTR) {
                lhs_val = irb.CreateIntToPtr(lhs_val, llvm::PointerType::get(ctx, 0));

                return irb.CreateICmpUGE(lhs_val, rhs_val);
            } else if (lhs_type->ty_Int.is_signed) {
                return irb.CreateICmpSGE(lhs_val, rhs_val);
            } else {
                return irb.CreateICmpUGE(lhs_val, rhs_val);
            }
        } else {
            Assert(lhs_type->kind == TYPE_FLOAT, "invalid types for GE op in codegen");
            return irb.CreateFCmpOGE(lhs_val, rhs_val);
        }
        break;
    case HIROP_BWAND:
        if (lhs_type->kind == TYPE_PTR) {
            lhs_val = irb.CreatePtrToInt(lhs_val, ll_platform_int_type);
            rhs_val = irb.CreateIntCast(rhs_val, ll_platform_int_type, rhs_type->ty_Int.is_signed);

            auto* result = irb.CreateAnd(lhs_val, rhs_val);
            return irb.CreateIntToPtr(result, llvm::PointerType::get(ctx, 0));
        } else if (rhs_type->kind == TYPE_PTR) {
            lhs_val = irb.CreateIntCast(lhs_val, ll_platform_int_type, lhs_type->ty_Int.is_signed);
            rhs_val = irb.CreatePtrToInt(rhs_val, ll_platform_int_type);

            auto* result = irb.CreateAnd(lhs_val, rhs_val);
            return irb.CreateIntToPtr(result, llvm::PointerType::get(ctx, 0));
        } else {
            Assert(lhs_type->kind == TYPE_INT, "invalid types for BWAND op in codegen");
            return irb.CreateAnd(lhs_val, rhs_val);
        }
        break;
    case HIROP_BWOR:
        if (lhs_type->kind == TYPE_PTR) {
            lhs_val = irb.CreatePtrToInt(lhs_val, ll_platform_int_type);
            rhs_val = irb.CreateIntCast(rhs_val, ll_platform_int_type, rhs_type->ty_Int.is_signed);

            auto* result = irb.CreateOr(lhs_val, rhs_val);
            return irb.CreateIntToPtr(result, llvm::PointerType::get(ctx, 0));
        } else if (rhs_type->kind == TYPE_PTR) {
            lhs_val = irb.CreateIntCast(lhs_val, ll_platform_int_type, lhs_type->ty_Int.is_signed);
            rhs_val = irb.CreatePtrToInt(rhs_val, ll_platform_int_type);

            auto* result = irb.CreateOr(lhs_val, rhs_val);
            return irb.CreateIntToPtr(result, llvm::PointerType::get(ctx, 0));
        } else {
            Assert(lhs_type->kind == TYPE_INT, "invalid types for BWOR op in codegen");
            return irb.CreateOr(lhs_val, rhs_val);
        }
        break;
    case HIROP_BWXOR:
        if (lhs_type->kind == TYPE_PTR) {
            lhs_val = irb.CreatePtrToInt(lhs_val, ll_platform_int_type);
            rhs_val = irb.CreateIntCast(rhs_val, ll_platform_int_type, rhs_type->ty_Int.is_signed);

            auto* result = irb.CreateXor(lhs_val, rhs_val);
            return irb.CreateIntToPtr(result, llvm::PointerType::get(ctx, 0));
        } else if (rhs_type->kind == TYPE_PTR) {
            lhs_val = irb.CreateIntCast(lhs_val, ll_platform_int_type, lhs_type->ty_Int.is_signed);
            rhs_val = irb.CreatePtrToInt(rhs_val, ll_platform_int_type);

            auto* result = irb.CreateXor(lhs_val, rhs_val);
            return irb.CreateIntToPtr(result, llvm::PointerType::get(ctx, 0));
        } else {
            Assert(lhs_type->kind == TYPE_INT, "invalid types for BWXOR op in codegen");
            return irb.CreateXor(lhs_val, rhs_val);
        }
        break;
    }

    Panic("unsupported binary operator in codegen: {}", (int)node->ir_Binop.op);
}

llvm::Value* CodeGenerator::genStrEq(llvm::Value* lhs, llvm::Value* rhs) {
    auto* cmp_result = irb.CreateCall(rtstub_strcmp, { lhs, rhs });
    return irb.CreateICmpEQ(cmp_result, getPlatformIntConst(0));
}

llvm::Value* CodeGenerator::genUnop(HirExpr* node) {
    auto* x_type = node->ir_Unop.expr->type->Inner();
    auto* x_val = genExpr(node->ir_Unop.expr);

    switch (node->ir_Unop.op) {
    case HIROP_NEG:
        if (x_type->kind == TYPE_INT) {
            return irb.CreateNeg(x_val);
        } else {
            Assert(x_type->kind == TYPE_FLOAT, "invalid type for NEG in codegen");
            return irb.CreateFNeg(x_val);
        }
        break;
    case HIROP_NOT:
        Assert(x_type->kind == TYPE_BOOL, "invalid type for NOT in codegen");
        return irb.CreateNot(x_val);
    case HIROP_BWNEG:
        Assert(x_type->kind == TYPE_INT, "invalid type for BWNEG in codegen");
        return irb.CreateNot(x_val);
    }

    Panic("unsupported unary operator in codegen: {}", (int)node->ir_Unop.op);
    return nullptr;
}

/* -------------------------------------------------------------------------- */

void CodeGenerator::genDivideByZeroCheck(llvm::Value* divisor, Type* int_type) {
    auto* is_zero_val = irb.CreateICmpEQ(divisor, makeLLVMIntLit(int_type, 0));
    is_zero_val = genLLVMExpect(is_zero_val, makeLLVMIntLit(&prim_bool_type, 0));

    auto* bb_zero = appendBlock();
    auto* bb_nonzero = appendBlock();

    irb.CreateCondBr(is_zero_val, bb_zero, bb_nonzero);

    setCurrentBlock(bb_zero);
    
    if (rtstub_panic_divide == nullptr)
        rtstub_panic_divide = genPanicStub("__berry_panicDivide");

    irb.CreateCall(rtstub_panic_divide);
    irb.CreateUnreachable();

    setCurrentBlock(bb_nonzero);
}

void CodeGenerator::genDivideOverflowCheck(llvm::Value* dividend, llvm::Value* divisor, Type* int_type) {
    uint64_t max_neg_int = 1 << (int_type->ty_Int.bit_size - 1); 
    auto* is_max_neg_int = irb.CreateICmpEQ(dividend, makeLLVMIntLit(int_type, max_neg_int));
    auto* is_neg_one = irb.CreateICmpEQ(divisor, makeLLVMIntLit(int_type, -1));

    auto* is_div_overflow = irb.CreateAnd(is_max_neg_int, is_neg_one);
    is_div_overflow = genLLVMExpect(is_div_overflow, makeLLVMIntLit(&prim_bool_type, 0));
    
    auto* bb_overflow = appendBlock();
    auto* bb_ok = appendBlock();

    irb.CreateCondBr(is_div_overflow, bb_overflow, bb_ok);
    setCurrentBlock(bb_overflow);

    if (rtstub_panic_overflow == nullptr)
        rtstub_panic_overflow = genPanicStub("__berry_panicOverflow");

    irb.CreateCall(rtstub_panic_overflow);
    irb.CreateUnreachable();

    setCurrentBlock(bb_ok);
}

void CodeGenerator::genShiftOverflowCheck(llvm::Value* rhs, Type* int_type) {
    auto* is_good_shift = irb.CreateICmpULT(rhs, makeLLVMIntLit(int_type, int_type->ty_Int.bit_size));
    is_good_shift = genLLVMExpect(is_good_shift, makeLLVMIntLit(&prim_bool_type, 1));

    auto* bb_bad_shift = appendBlock();
    auto* bb_good_shift = appendBlock();

    irb.CreateCondBr(is_good_shift, bb_good_shift, bb_bad_shift);
    setCurrentBlock(bb_bad_shift);

    if (rtstub_panic_overflow == nullptr)
        rtstub_panic_overflow = genPanicStub("__berry_panicOverflow");

    irb.CreateCall(rtstub_panic_overflow);
    irb.CreateUnreachable();

    setCurrentBlock(bb_good_shift);
}

llvm::Value* CodeGenerator::genLLVMExpect(llvm::Value* value, llvm::Value* expected) {
    return irb.CreateIntrinsic(value->getType(), llvm::Intrinsic::expect, { value, expected });
}

#include "codegen.hpp"

llvm::Value* CodeGenerator::genExpr(AstExpr* node, bool expect_addr, llvm::Value* alloc_loc) {
    debug.SetDebugLocation(node->span);

    switch (node->kind) {
    case AST_TEST_MATCH:
        return genTestMatch(node);
    case AST_CAST:
        return genCast(node);
    case AST_BINOP:
        return genBinop(node);
    case AST_UNOP:
        return genUnop(node);
    case AST_ADDR:
        Assert(node->an_Addr.elem->IsLValue(), "tried to take address of r-value in codegen");
        return genExpr(node->an_Addr.elem, true);
    case AST_DEREF: {
        auto ptr_val = genExpr(node->an_Deref.ptr);
    
        if (expect_addr || shouldPtrWrap(node->type)) {
            return ptr_val;
        } else {
            return irb.CreateLoad(genType(node->type), ptr_val);
        }
    } break;
    case AST_CALL:
        return genCall(node, alloc_loc);
    case AST_INDEX:
        return genIndexExpr(node, expect_addr);
    case AST_SLICE:
        return genSliceExpr(node, alloc_loc);
    case AST_FIELD:
        return genFieldExpr(node, expect_addr);
    case AST_STATIC_GET: {
        auto& afield = node->an_Field;

        auto* ll_value = loaded_imports[afield.root->an_Ident.dep_id][afield.imported_sym->def_number];
        if (!expect_addr && (afield.imported_sym->flags & SYM_VAR) && !shouldPtrWrap(node->type)) {
            return irb.CreateLoad(genType(node->type), ll_value);
        }

        return ll_value;
    } break;
    case AST_ARRAY:
        return genArrayLit(node, alloc_loc);
    case AST_NEW:
        return genNewExpr(node, alloc_loc);
    case AST_STRUCT_LIT_POS:
    case AST_STRUCT_LIT_NAMED:
    case AST_STRUCT_PTR_LIT_POS:
    case AST_STRUCT_PTR_LIT_NAMED:
        return genStructLit(node, alloc_loc);
    case AST_ENUM_LIT: 
        return node->type->FullUnwrap()->ty_Enum.tag_values[node->an_Field.field_index];
    case AST_IDENT: 
        return genIdent(node, expect_addr);
    case AST_INT: {
        // NOTE: It is possible for an int literal to have a float type if a
        // number literal implicitly takes on a floating point value.
        auto inner_type = node->type->FullUnwrap();
        switch (inner_type->kind) {
        case TYPE_INT: 
            return makeLLVMIntLit(inner_type, node->an_Int.value);
        case TYPE_FLOAT: 
            return makeLLVMFloatLit(inner_type, (double)node->an_Int.value);
        case TYPE_ENUM:
            return makeLLVMIntLit(platform_int_type, node->an_Int.value);
        case TYPE_PTR: {
            auto* int_lit = makeLLVMIntLit(platform_uint_type, node->an_Int.value);
            return irb.CreateIntToPtr(int_lit, llvm::PointerType::get(ctx, 0));
        } break;
        default:
            Panic("non-numeric type integer literal in codegen");
        }    
    } break;
    case AST_FLOAT: {
        return makeLLVMFloatLit(node->type->Inner(), node->an_Float.value);
    } break;
    case AST_BOOL:
        return makeLLVMIntLit(node->type, (uint64_t)node->an_Bool.value);
    case AST_NULL:
        return getNullValue(genType(node->type));
    case AST_STRING:
        return genStrLit(node, alloc_loc);
    case AST_MACRO_SIZEOF:
        return makeLLVMIntLit(platform_uint_type, getLLVMByteSize(genType(node->an_TypeMacro.type_arg, true)));
    case AST_MACRO_ALIGNOF:
        return makeLLVMIntLit(platform_uint_type, getLLVMByteAlign(genType(node->an_TypeMacro.type_arg, true)));
    case AST_MACRO_FUNCADDR:
        // TODO: make this more sophisticated later...
        return genExpr(node->an_ValueMacro.expr);
    default:
        Panic("expr codegen not implemented for {}", (int)node->kind);
        break;
    }

    return nullptr;
}

/* -------------------------------------------------------------------------- */

llvm::Value* CodeGenerator::genTestMatch(AstExpr* node) {
    auto* true_block = appendBlock();
    auto* false_block = appendBlock();
    auto* end_block = appendBlock();

    genPatternMatch(node->an_TestMatch.expr, { { node->an_TestMatch.pattern, true_block } }, false_block);

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

llvm::Value* CodeGenerator::genCast(AstExpr* node) {
    auto& acast = node->an_Cast;

    auto* src_val = genExpr(acast.src);
    if (tctx.Equal(acast.src->type, node->type)) {
        return src_val;
    }

    auto* src_type = acast.src->type->FullUnwrap();
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
    case TYPE_SLICE:
        if (src_kind == TYPE_STRING)
            return src_val;
        break;
    case TYPE_STRING:
        if (src_kind == TYPE_SLICE)
            return src_val;
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

llvm::Value* CodeGenerator::genBinop(AstExpr* node) {
    auto lhs_val = genExpr(node->an_Binop.lhs);

    // Handle short circuit operators.
    if (node->an_Binop.op == AOP_LGAND) {
        auto* start_block = getCurrentBlock(); 
        auto* true_block = appendBlock();
        auto* end_block = appendBlock();

        irb.CreateCondBr(lhs_val, true_block, end_block);

        setCurrentBlock(true_block);
        auto* rhs_val = genExpr(node->an_Binop.rhs);
        irb.CreateBr(end_block);

        setCurrentBlock(end_block);
        auto* phi_node = irb.CreatePHI(llvm::Type::getInt1Ty(ctx), 2);
        phi_node->addIncoming(lhs_val, start_block);
        phi_node->addIncoming(rhs_val, true_block);
        return phi_node;
    } else if (node->an_Binop.op == AOP_LGOR) {
        auto* start_block = getCurrentBlock();
        auto* false_block = appendBlock();
        auto* end_block = appendBlock();

        irb.CreateCondBr(lhs_val, end_block, false_block);

        setCurrentBlock(false_block);
        auto* rhs_val = genExpr(node->an_Binop.rhs);
        irb.CreateBr(end_block);

        setCurrentBlock(end_block);
        auto* phi_node = irb.CreatePHI(llvm::Type::getInt1Ty(ctx), 2);
        phi_node->addIncoming(lhs_val, start_block);
        phi_node->addIncoming(rhs_val, false_block);
        return phi_node;
    }

    auto* lhs_type = node->an_Binop.lhs->type->Inner();
    auto* rhs_type = node->an_Binop.rhs->type->Inner();
    auto* rhs_val = genExpr(node->an_Binop.rhs);
    switch (node->an_Binop.op) {
    case AOP_ADD:
        if (lhs_type->kind == TYPE_PTR) {
            if (rhs_type->kind == TYPE_PTR) {
                lhs_val = irb.CreatePtrToInt(lhs_val, ll_platform_int_type);
                rhs_val = irb.CreatePtrToInt(rhs_val, ll_platform_int_type);
                auto* sum = irb.CreateAdd(lhs_val, rhs_val);
                return irb.CreateIntToPtr(sum, llvm::PointerType::get(ctx, 0));
            } else {
                return irb.CreateGEP(genType(lhs_type->ty_Ptr.elem_type, true), lhs_val, { rhs_val });
            }
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
    case AOP_SUB:
        if (lhs_type->kind == TYPE_PTR) {
            if (rhs_type->kind == TYPE_PTR) {
                lhs_val = irb.CreatePtrToInt(lhs_val, ll_platform_int_type);
                rhs_val = irb.CreatePtrToInt(rhs_val, ll_platform_int_type);
                auto* diff = irb.CreateSub(lhs_val, rhs_val);
                return irb.CreateIntToPtr(diff, llvm::PointerType::get(ctx, 0));
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
    case AOP_MUL:
        if (lhs_type->kind == TYPE_INT) {
            return irb.CreateMul(lhs_val, rhs_val);
        } else {
            Assert(lhs_type->kind == TYPE_FLOAT, "invalid types for MUL op in codegen");
            return irb.CreateFMul(lhs_val, rhs_val);
        }
        break;
    case AOP_DIV:
        if (lhs_type->kind == TYPE_INT) {
            if (lhs_type->ty_Int.is_signed) {
                return irb.CreateSDiv(lhs_val, rhs_val);
            } else {
                return irb.CreateUDiv(lhs_val, rhs_val);
            }
        } else {
            Assert(lhs_type->kind == TYPE_FLOAT, "invalid types for DIV op in codegen");
            return irb.CreateFDiv(lhs_val, rhs_val);         
        }
        break;
    case AOP_MOD:
        if (lhs_type->kind == TYPE_INT) {
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
    case AOP_SHL:
        Assert(lhs_type->kind == TYPE_INT, "invalid types for SHL op in codegen");
        return irb.CreateShl(lhs_val, rhs_val);
    case AOP_SHR:
        Assert(lhs_type->kind == TYPE_INT, "invalid types for SHR op in codegen");

        if (lhs_type->ty_Int.is_signed) {
            return irb.CreateAShr(lhs_val, rhs_val);
        } else {
            return irb.CreateLShr(lhs_val, rhs_val);
        }
    break;
    case AOP_EQ:
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
    case AOP_NE:
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
    case AOP_LT:
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
    case AOP_GT:
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
    case AOP_LE:
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
    case AOP_GE:
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
    case AOP_BWAND:
        Assert(lhs_type->kind == TYPE_INT, "invalid types for BWAND op in codegen");
        return irb.CreateAnd(lhs_val, rhs_val);
    case AOP_BWOR:
        Assert(lhs_type->kind == TYPE_INT, "invalid types for BWOR op in codegen");
        return irb.CreateOr(lhs_val, rhs_val);
    case AOP_BWXOR:
        Assert(lhs_type->kind == TYPE_INT, "invalid types for BWXOR op in codegen");
        return irb.CreateXor(lhs_val, rhs_val);   
    }

    Panic("unsupported binary operator in codegen: {}", (int)node->an_Binop.op);
}

llvm::Value* CodeGenerator::genStrEq(llvm::Value* lhs, llvm::Value* rhs) {
    auto* cmp_result = irb.CreateCall(rtstub_strcmp, { lhs, rhs });
    return irb.CreateICmpEQ(cmp_result, getPlatformIntConst(0));
}

llvm::Value* CodeGenerator::genUnop(AstExpr* node) {
    auto* operand_type = node->an_Unop.operand->type->Inner();
    auto* operand_val = genExpr(node->an_Unop.operand);
    switch (node->an_Unop.op) {
    case AOP_NEG:
        if (operand_type->kind == TYPE_INT) {
            return irb.CreateNeg(operand_val);
        } else {
            Assert(operand_type->kind == TYPE_FLOAT, "invalid type for NEG in codegen");
            return irb.CreateFNeg(operand_val);
        }
        break;
    case AOP_NOT:
        Assert(operand_type->kind == TYPE_BOOL, "invalid type for NOT in codegen");
        return irb.CreateNot(operand_val);
    case AOP_BWNEG:
        Assert(operand_type->kind == TYPE_INT, "invalid type for BWNEG in codegen");
        return irb.CreateNot(operand_val);
    }

    Panic("unsupported unary operator in codegen: {}", (int)node->an_Unop.op);
    return nullptr;
}

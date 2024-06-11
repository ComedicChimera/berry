#include "checker.hpp"

#include "target.hpp"

uint64_t Checker::checkComptimeSize(AstNode* node) {
    comptime_depth++;
    auto* expr = checkExpr(node, platform_int_type);
    comptime_depth--;
    
    mustIntType(expr->span, expr->type);
    finishExpr();

    uint64_t size;
    if (evalComptimeSizeValue(expr, &size)) {
        return size;
    } else {
        fatal(node->span, "compile time size cannot be less than zero");
    }

    return 0;
}

/* -------------------------------------------------------------------------- */

ConstValue* Checker::evalComptime(HirExpr* node) {
    ConstValue* value;

    switch (node->kind) {
    case HIR_CAST:
        value = evalComptimeCast(node);
        break;
    case HIR_BINOP:
        value = evalComptimeBinaryOp(node);
        break;
    case HIR_UNOP:
        value = evalComptimeUnaryOp(node);
        break;
    case HIR_INDEX: 
        value = evalComptimeIndex(node);
        break;
    case HIR_SLICE:
        value = evalComptimeSlice(node);
        break;
    case HIR_FIELD: {
        auto* root_value = evalComptime(node->ir_Field.expr);

        if (root_value->kind == CONST_ARRAY) {
            Assert(node->ir_Field.field_index == 1, "access to array._ptr in comptime expr");

            if (platform_int_type->ty_Int.bit_size == 64) {
                value = allocComptime(CONST_I64);
                value->v_i64 = (int64_t)root_value->v_array.elems.size();
            } else {
                value = allocComptime(CONST_I32);
                value->v_i32 = (int32_t)root_value->v_array.elems.size();
            }
        } else if (root_value->kind == CONST_STRING) {
            Assert(node->ir_Field.field_index == 1, "access to string._ptr in comptime expr");

            if (platform_int_type->ty_Int.bit_size == 64) {
                value = allocComptime(CONST_I64);
                value->v_i64 = (int64_t)root_value->v_array.elems.size();
            } else {
                value = allocComptime(CONST_I32);
                value->v_i32 = (int32_t)root_value->v_array.elems.size();
            }
        } else {
            Assert(root_value->kind == CONST_STRUCT, "invalid comptime field access");
            value = root_value->v_struct.fields[node->ir_Field.field_index];
        }
    } break;
    case HIR_ARRAY_LIT: {
        value = allocComptime(CONST_ARRAY);

        std::vector<ConstValue*> elems;
        for (auto* helem : node->ir_ArrayLit.items) {
            elems.push_back(evalComptime(helem));
        }

        value->v_array.elems = arena.MoveVec(std::move(elems));
        value->v_array.elem_type = node->type->Inner()->ty_Slice.elem_type->Inner();
        value->v_array.mod_id = mod.id;
        value->v_array.alloc_loc = nullptr;
    } break;
    case HIR_STRUCT_LIT:
        value = evalComptimeStructLit(node);
        break;
    case HIR_ENUM_LIT:
        value = allocComptime(CONST_ENUM);
        value->v_enum = node->ir_EnumLit.tag_value;
        break;
    case HIR_STATIC_GET: {
        auto* symbol = node->ir_StaticGet.imported_symbol;
        Assert(symbol->flags & SYM_COMPTIME, "comptime eval with non-comptime symbol");

        if (symbol->flags & SYM_FUNC) {
            auto* value = allocComptime(CONST_FUNC);
            value->v_func = symbol;
        } else {
            auto* decl = mod.deps[node->ir_StaticGet.dep_id].mod->decls[symbol->decl_num];
            Assert(decl->hir_decl != nullptr && decl->hir_decl->kind == HIR_GLOBAL_CONST, "invalid comptime symbol");
            value = decl->hir_decl->ir_GlobalConst.init;
        }
    } break;
    case HIR_IDENT: {
        auto* symbol = node->ir_Ident.symbol;
        Assert(symbol->flags & SYM_COMPTIME, "comptime eval with non-comptime symbol");

        if (symbol->flags & SYM_FUNC) {
            auto* value = allocComptime(CONST_FUNC);
            value->v_func = symbol;
        } else {
            auto* decl = mod.decls[symbol->decl_num];
            Assert(decl->hir_decl != nullptr && decl->hir_decl->kind == HIR_GLOBAL_CONST, "invalid comptime symbol");
            value = decl->hir_decl->ir_GlobalConst.init;
        }
    } break;
    case HIR_NUM_LIT: {
        auto& int_type = node->type->Inner()->ty_Int;

        switch (int_type.bit_size) {
        case 8:
            value = allocComptime(CONST_U8);
            value->v_u8 = (uint8_t)node->ir_Num.value;
            break;
        case 16:
            value = allocComptime(CONST_U16);
            value->v_u16 = (uint16_t)node->ir_Num.value;
            break;
        case 32:
            value = allocComptime(CONST_U32);
            value->v_u32 = (uint32_t)node->ir_Num.value;
            break;
        case 64:
            value = allocComptime(CONST_U64);
            value->v_u64 = node->ir_Num.value;
            break;
        }

        if (int_type.is_signed) {
            value->kind = (ConstKind)((int)value->kind - 1);
        }
    } break;
    case HIR_FLOAT_LIT:
        if (node->type->Inner()->ty_Float.bit_size == 32) {
            value = allocComptime(CONST_F32);
            value->v_f32 = (float)node->ir_Float.value;
        } else {
            value = allocComptime(CONST_F64);
            value->v_f64 = node->ir_Float.value;
        }
        break;
    case HIR_BOOL_LIT:
        value = allocComptime(CONST_BOOL);
        value->v_bool = node->ir_Bool.value;
        break;
    case HIR_STRING_LIT:
        value = allocComptime(CONST_STRING);
        value->v_str.value = node->ir_String.value;
        value->v_str.mod_id = mod.id;
        value->v_str.alloc_loc = nullptr;
        break;
    case HIR_NULL:
        value = getComptimeNull(node->type);
        break;
    case HIR_MACRO_SIZEOF: {
        auto size = GetTargetPlatform().GetComptimeSizeOf(node->ir_MacroType.arg);

        if (platform_int_type->ty_Int.bit_size == 64) {
            value = allocComptime(CONST_I64);
            value->v_i64 = (int64_t)size;
        } else {
            value = allocComptime(CONST_I32);
            value->v_i32 = (int32_t)size;
        }
    } break;
    case HIR_MACRO_ALIGNOF: {
        auto align = GetTargetPlatform().GetComptimeAlignOf(node->ir_MacroType.arg);

        if (platform_int_type->ty_Int.bit_size == 64) {
            value = allocComptime(CONST_I64);
            value->v_i64 = (int64_t)align;
        } else {
            value = allocComptime(CONST_I32);
            value->v_i32 = (int32_t)align;
        }
    } break;
    default:
        Panic("comptime evaluation not implemented for AST node");
        break;
    }

    return value;
}

#define COMPTIME_INT_CAST(DEST_, TYPE_) switch (src->kind) { \
    case CONST_I8: \
        value->DEST_ = (TYPE_)src->v_i8; \
        break; \
    case CONST_U8: \
        value->DEST_ = (TYPE_)src->v_u8; \
        break; \
    case CONST_I16: \
        value->DEST_ = (TYPE_)src->v_i16; \
        break; \
    case CONST_U16: \
        value->DEST_ = (TYPE_)src->v_u16; \
        break; \
    case CONST_I32: \
        value->DEST_ = (TYPE_)src->v_i32; \
        break; \
    case CONST_U32: \
        value->DEST_ = (TYPE_)src->v_u32; \
        break; \
    case CONST_I64: \
        value->DEST_ = (TYPE_)src->v_i64; \
        break; \
    case CONST_U64: \
        value->DEST_ = (TYPE_)src->v_u64; \
        break; \
    }

#define COMPTIME_TO_INT_CAST(DEST_, TYPE_) switch (src->kind) { \
    case CONST_I8: \
        value->DEST_ = (TYPE_)src->v_i8; \
    case CONST_U8: \
        value->DEST_ = (TYPE_)src->v_u8; \
    case CONST_I16: \
        value->DEST_ = (TYPE_)src->v_i16; \
    case CONST_U16: \
        value->DEST_ = (TYPE_)src->v_u16; \
    case CONST_I32: \
        value->DEST_ = (TYPE_)src->v_i32; \
    case CONST_U32: \
        value->DEST_ = (TYPE_)src->v_u32; \
    case CONST_I64: \
        value->DEST_ = (TYPE_)src->v_i64; \
    case CONST_U64: \
        value->DEST_ = (TYPE_)src->v_u64; \
    case CONST_F32: \
        value->DEST_ = (TYPE_)src->v_f32; \
    case CONST_F64: \
        value->DEST_ = (TYPE_)src->v_f64; \
    case CONST_BOOL: \
        value->DEST_ = (TYPE_)src->v_bool; \
    case CONST_ENUM: \
        value->DEST_ = (TYPE_)src->v_enum; \
    }

ConstValue* Checker::evalComptimeCast(HirExpr* node) {
    auto* src = evalComptime(node->ir_Cast.expr);

    auto* dest_type = node->type->Inner();
    if (tctx.Equal(node->ir_Cast.expr->type, dest_type)) {
        return src;
    }

    ConstValue* value;
    switch (dest_type->kind) {
    case TYPE_BOOL:
        value = allocComptime(CONST_BOOL);
        COMPTIME_INT_CAST(v_bool, bool);
        break;
    case TYPE_INT:
        if (dest_type->ty_Int.is_signed) {
            switch (dest_type->ty_Int.bit_size) {
            case 8:
                value = allocComptime(CONST_I8);
                COMPTIME_TO_INT_CAST(v_i8, int8_t);
                break;
            case 16:
                value = allocComptime(CONST_I16);
                COMPTIME_TO_INT_CAST(v_i16, int16_t);
                break;
            case 32:
                value = allocComptime(CONST_I32);
                COMPTIME_TO_INT_CAST(v_i32, int32_t);
                break;
            case 64:
                value = allocComptime(CONST_I64);
                COMPTIME_TO_INT_CAST(v_i64, int64_t);
                break;
            }
        } else {
            switch (dest_type->ty_Int.bit_size) {
            case 8:
                value = allocComptime(CONST_U8);
                COMPTIME_TO_INT_CAST(v_u8, uint8_t);
                break;
            case 16:
                value = allocComptime(CONST_U16);
                COMPTIME_TO_INT_CAST(v_u16, uint16_t);
                break;
            case 32:
                value = allocComptime(CONST_U32);
                COMPTIME_TO_INT_CAST(v_u32, uint32_t);
                break;
            case 64:
                value = allocComptime(CONST_U64);
                COMPTIME_TO_INT_CAST(v_u64, uint64_t);
                break;
            }
        }
        break;
    case TYPE_FLOAT:
        if (dest_type->ty_Float.bit_size == 32) {
            value = allocComptime(CONST_F32);
            if (src->kind == CONST_F64) {
                value->v_f32 = (float)src->v_f64;
            } else {
                COMPTIME_INT_CAST(v_f32, float);
            }
        } else {
            value = allocComptime(CONST_F64);
            if (src->kind == CONST_F32) {
                value->v_f64 = (double)src->v_f32;
            } else {
                COMPTIME_INT_CAST(v_f64, double);
            }
        }
        break;
    case TYPE_PTR:
        if (src->kind == CONST_PTR) {
            return src;
        } else {
            value = allocComptime(CONST_PTR);
            COMPTIME_INT_CAST(v_ptr, size_t);
        }
        break;
    case TYPE_ARRAY:
    case TYPE_SLICE:
        if (src->kind == CONST_STRING) {
            value = allocComptime(CONST_ARRAY);

            std::vector<ConstValue*> values;
            for (auto ch : src->v_str.value) {
                auto* ch_value = allocComptime(CONST_U8);
                ch_value->v_u8 = ch;
                values.push_back(ch_value);
            }

            value->v_array.elems = arena.MoveVec(std::move(values));
            value->v_array.elem_type = &prim_u8_type;
            value->v_array.mod_id = mod.id;
            value->v_array.alloc_loc = nullptr;
        } else if (src->kind == CONST_ARRAY || src->kind == CONST_ZERO_ARRAY) {
            value = src;
        }
        break;
    case TYPE_STRING:
        value = allocComptime(CONST_STRING);

        if (src->kind == CONST_ARRAY) {
            std::string str_data;
            for (auto* cv : src->v_array.elems) {
                str_data.push_back(cv->v_u8);
            }

            value->v_str.value = arena.MoveStr(std::move(str_data));
        } else if (src->kind == CONST_ZERO_ARRAY) {
            std::string str_data(src->v_zarr.num_elems, 0);
            
            value->v_str.value = arena.MoveStr(std::move(str_data));
        } else {
            Panic("unimplemented comptime cast");
        }

        value->v_str.mod_id = mod.id;
        value->v_str.alloc_loc = nullptr;
        break;
    case TYPE_ENUM:
        value = allocComptime(CONST_ENUM);
        COMPTIME_INT_CAST(v_enum, uint64_t);
        break;
    }

    if (value == nullptr)
        Panic("unimplemented comptime cast");

    return value;
}

#define COMPTIME_NUM_BINOP(OP_) Assert(lhs->kind == rhs->kind, "invalid comptime binary op"); \
    switch (lhs->kind) { \
    case CONST_I8: \
        value = allocComptime(CONST_I8); \
        value->v_i8 = lhs->v_i8 OP_ rhs->v_i8; \
        break; \
    case CONST_U8: \
        value = allocComptime(CONST_U8); \
        value->v_u8 = lhs->v_u8 OP_ rhs->v_u8; \
        break; \
    case CONST_I16: \
        value = allocComptime(CONST_I16); \
        value->v_i16 = lhs->v_i16 OP_ rhs->v_i16; \
        break; \
    case CONST_U16: \
        value = allocComptime(CONST_U16); \
        value->v_u16 = lhs->v_u16 OP_ rhs->v_u16; \
        break; \
    case CONST_I32: \
        value = allocComptime(CONST_I32); \
        value->v_i32 = lhs->v_i32 OP_ rhs->v_i32; \
        break; \
    case CONST_U32: \
        value = allocComptime(CONST_U32); \
        value->v_u32 = lhs->v_u32 OP_ rhs->v_u32; \
        break; \
    case CONST_I64: \
        value = allocComptime(CONST_I64); \
        value->v_i64 = lhs->v_i64 OP_ rhs->v_i64; \
        break; \
    case CONST_U64: \
        value = allocComptime(CONST_U64); \
        value->v_u64 = lhs->v_u64 OP_ rhs->v_u64; \
        break; \
    case CONST_F32: \
        value = allocComptime(CONST_F32); \
        value->v_f32 = lhs->v_f32 OP_ rhs->v_f32; \
        break; \
    case CONST_F64: \
        value = allocComptime(CONST_F64); \
        value->v_f64 = lhs->v_f64 OP_ rhs->v_f64; \
        break; \
    }

#define COMPTIME_INT_BINOP(OP_) Assert(lhs->kind == rhs->kind, "invalid comptime binary op"); \
    switch (lhs->kind) { \
    case CONST_I8: \
        value = allocComptime(CONST_I8); \
        value->v_i8 = lhs->v_i8 OP_ rhs->v_i8; \
        break; \
    case CONST_U8: \
        value = allocComptime(CONST_U8); \
        value->v_u8 = lhs->v_u8 OP_ rhs->v_u8; \
        break; \
    case CONST_I16: \
        value = allocComptime(CONST_I16); \
        value->v_i16 = lhs->v_i16 OP_ rhs->v_i16; \
        break; \
    case CONST_U16: \
        value = allocComptime(CONST_U16); \
        value->v_u16 = lhs->v_u16 OP_ rhs->v_u16; \
        break; \
    case CONST_I32: \
        value = allocComptime(CONST_I32); \
        value->v_i32 = lhs->v_i32 OP_ rhs->v_i32; \
        break; \
    case CONST_U32: \
        value = allocComptime(CONST_U32); \
        value->v_u32 = lhs->v_u32 OP_ rhs->v_u32; \
        break; \
    case CONST_I64: \
        value = allocComptime(CONST_I64); \
        value->v_i64 = lhs->v_i64 OP_ rhs->v_i64; \
        break; \
    case CONST_U64: \
        value = allocComptime(CONST_U64); \
        value->v_u64 = lhs->v_u64 OP_ rhs->v_u64; \
        break; \
    }

#define COMPTIME_COMP_NUM_BINOP(OP_) Assert(lhs->kind == rhs->kind, "invalid comptime binary op"); \
    value = allocComptime(CONST_BOOL); \
    switch (lhs->kind) { \
    case CONST_I8: \
        value->v_bool = lhs->v_i8 OP_ rhs->v_i8; \
        break; \
    case CONST_U8: \
        value->v_bool = lhs->v_u8 OP_ rhs->v_u8; \
        break; \
    case CONST_I16: \
        value->v_bool = lhs->v_i16 OP_ rhs->v_i16; \
        break; \
    case CONST_U16: \
        value->v_bool = lhs->v_u16 OP_ rhs->v_u16; \
        break; \
    case CONST_I32: \
        value->v_bool = lhs->v_i32 OP_ rhs->v_i32; \
        break; \
    case CONST_U32: \
        value->v_bool = lhs->v_u32 OP_ rhs->v_u32; \
        break; \
    case CONST_I64: \
        value->v_bool = lhs->v_i64 OP_ rhs->v_i64; \
        break; \
    case CONST_U64: \
        value->v_bool = lhs->v_u64 OP_ rhs->v_u64; \
        break; \
    case CONST_F32: \
        value->v_bool = lhs->v_f32 OP_ rhs->v_f32; \
        break;  \
    case CONST_F64: \
        value->v_bool = lhs->v_f64 OP_ rhs->v_f64; \
        break; \
    }

static bool checkComptimeNonzero(ConstValue* value) {
    switch (value->kind) {
    case CONST_I8:
        if (value->v_i8 == 0)
            return true;
        break;
    case CONST_U8:
        if (value->v_u8 == 0)
            return true;
        break;
    case CONST_I16:
        if (value->v_i16 == 0)
            return true;
        break;
    case CONST_U16:
        if (value->v_u16 == 0)
            return true;
        break;
    case CONST_I32:
        if (value->v_i32 == 0)
            return true;
        break;
    case CONST_U32:
        if (value->v_u32 == 0)
            return true;
        break;
    case CONST_I64:
        if (value->v_i64 == 0)
            return true;
        break;
    case CONST_U64:
        if (value->v_u64 == 0)
            return true;
        break;
    }

    return false;
}

ConstValue* Checker::evalComptimeBinaryOp(HirExpr* node) {
    auto* lhs = evalComptime(node->ir_Binop.lhs);

    if (node->ir_Binop.op == HIROP_LGAND) {
        if (lhs->kind == CONST_BOOL) {
            return lhs->v_bool ? evalComptime(node->ir_Binop.rhs) : lhs;
        }
    } else if (node->ir_Binop.op == HIROP_LGOR) {
        if (lhs->kind == CONST_BOOL) {
            return lhs->v_bool ? lhs : evalComptime(node->ir_Binop.rhs);
        }
    }

    auto* rhs = evalComptime(node->ir_Binop.rhs);

    ConstValue* value = nullptr;
    switch (node->ir_Binop.op) {
    case HIROP_ADD:
        COMPTIME_NUM_BINOP(+);
        break;
    case HIROP_SUB:
        COMPTIME_NUM_BINOP(-);
        break;
    case HIROP_MUL:
        COMPTIME_NUM_BINOP(*);
        break;
    case HIROP_DIV:
        if (checkComptimeNonzero(rhs)) {
            comptimeEvalError(node->ir_Binop.rhs->span, "integer divide by zero");
        }
        COMPTIME_NUM_BINOP(/);
        break;
    case HIROP_MOD:
        if (checkComptimeNonzero(rhs)) {
            comptimeEvalError(node->ir_Binop.rhs->span, "integer divide by zero");
        }
        
        if (rhs->kind == CONST_F32) {
            value = allocComptime(CONST_F32);
            value->v_f32 = fmod(lhs->v_f32, rhs->v_f32);
        } else if (rhs->kind == CONST_F64) {
            value = allocComptime(CONST_F64);
            value->v_f64 = fmod(lhs->v_f64, rhs->v_f64);
        } else {
            COMPTIME_INT_BINOP(%);
        }
        break;
    case HIROP_SHL:
        COMPTIME_INT_BINOP(<<);
        break;
    case HIROP_SHR:
        COMPTIME_INT_BINOP(>>);
        break;
    case HIROP_BWAND:
        COMPTIME_INT_BINOP(&);
        break;
    case HIROP_BWOR:
        COMPTIME_INT_BINOP(|);
        break;
    case HIROP_BWXOR:
        COMPTIME_INT_BINOP(^);
        break;
    case HIROP_EQ:
        COMPTIME_COMP_NUM_BINOP(==);
        break;
    case HIROP_NE:
        COMPTIME_COMP_NUM_BINOP(!=);
        break;
    case HIROP_LT:
        COMPTIME_COMP_NUM_BINOP(<);
        break;
    case HIROP_GT:
        COMPTIME_COMP_NUM_BINOP(>);
        break;
    case HIROP_LE:
        COMPTIME_COMP_NUM_BINOP(<=);
        break;
    case HIROP_GE:
        COMPTIME_COMP_NUM_BINOP(>=);
        break;
    }

    if (value == nullptr)
        Panic("unimplemented comptime binary op");

    return value;
}

ConstValue* Checker::evalComptimeUnaryOp(HirExpr* node) {
    auto* operand = evalComptime(node->ir_Unop.expr);

    ConstValue* value = nullptr;
    switch (node->ir_Unop.op) {
    case HIROP_NEG:
        switch (operand->kind) {
        case CONST_I8:
            value = allocComptime(CONST_I8);
            value->v_i8 = -value->v_i8;
            break;
        case CONST_U8:
            value = allocComptime(CONST_U8);
            value->v_u8 = -value->v_u8;
            break;
        case CONST_I16:
            value = allocComptime(CONST_I16);
            value->v_i16 = -value->v_i16;
            break;
        case CONST_U16:
            value = allocComptime(CONST_U16);
            value->v_u16 = -value->v_u16;
            break;
        case CONST_I32:
            value = allocComptime(CONST_I32);
            value->v_i32 = -value->v_i32;
            break;
        case CONST_U32:
            value = allocComptime(CONST_U32);
            value->v_u32 = -value->v_u32;
            break;
        case CONST_I64:
            value = allocComptime(CONST_I64);
            value->v_i64 = -value->v_i64;
            break;
        case CONST_U64:
            value = allocComptime(CONST_U64);
            value->v_u64 = -value->v_u64;
            break;
        case CONST_F32:
            value = allocComptime(CONST_F32);
            value->v_f32 = -value->v_f32;
            break;
        case CONST_F64:
            value = allocComptime(CONST_F64);
            value->v_f64 = -value->v_f64;
            break;
        }
        break;
    case HIROP_BWNEG:
        switch (operand->kind) {
        case CONST_I8:
            value = allocComptime(CONST_I8);
            value->v_i8 = ~operand->v_i8;
            break;
        case CONST_U8:
            value = allocComptime(CONST_U8);
            value->v_u8 = ~operand->v_u8;
            break;
        case CONST_I16:
            value = allocComptime(CONST_I16);
            value->v_i16 = ~operand->v_i16;
            break;
        case CONST_U16:
            value = allocComptime(CONST_U16);
            value->v_u16 = ~operand->v_u16;
            break;
        case CONST_I32:
            value = allocComptime(CONST_I32);
            value->v_i32 = ~operand->v_i32;
            break;
        case CONST_U32:
            value = allocComptime(CONST_U32);
            value->v_u32 = ~operand->v_u32;
            break;
        case CONST_I64:
            value = allocComptime(CONST_I64);
            value->v_i64 = ~operand->v_i64;
            break;
        case CONST_U64:
            value = allocComptime(CONST_U64);
            value->v_u64 = ~operand->v_u64;
            break;
        }

        break;
    case HIROP_NOT:
        if (operand->kind == CONST_BOOL) {
            value = allocComptime(CONST_BOOL);
            value->v_bool = !operand->v_bool;
        }

        break;
    }

    Assert(value != nullptr, "unimplemented comptime unary operator");
    return value;
}

ConstValue* Checker::evalComptimeStructLit(HirExpr* node) {
    auto* struct_type = node->type->FullUnwrap();
    Assert(struct_type->kind == TYPE_STRUCT, "struct lit has non struct type");


    std::vector<ConstValue*> field_values(struct_type->ty_Struct.fields.size(), nullptr);
    for (auto& field : node->ir_StructLit.field_inits) {
        field_values[field.field_index] = evalComptime(field.expr);
    }

    for (size_t i = 0; i < field_values.size(); i++) {
        if (field_values[i] == nullptr) {
            field_values[i] = getComptimeNull(struct_type->ty_Struct.fields[i].type);
        }
    }

    auto* value = allocComptime(CONST_STRUCT);
    value->v_struct.fields = arena.MoveVec(std::move(field_values));
    value->v_struct.mod_id = mod.id;
    value->v_struct.alloc_loc = nullptr;
    return value;
}

ConstValue* Checker::evalComptimeIndex(HirExpr* node) {
    auto* array = evalComptime(node->ir_Index.expr);

    if (array->kind == CONST_ARRAY) {
        auto index = evalComptimeIndexValue(node->ir_Index.index, array->v_array.elems.size());

        return array->v_array.elems[index];
    } else if (array->kind == CONST_ZERO_ARRAY) {
        evalComptimeIndexValue(node->ir_Index.index, array->v_zarr.num_elems);

        return getComptimeNull(array->v_zarr.elem_type);
    } else if (array->kind == CONST_STRING) {
        auto index = evalComptimeIndexValue(node->ir_Index.index, array->v_str.value.size());

        auto* value = allocComptime(CONST_U8);
        value->v_u8 = array->v_str.value[index];
        return value;
    } else {
        Panic("invalid comptime index expr");
    }
}

ConstValue* Checker::evalComptimeSlice(HirExpr* node) {
    // NOTE: This particular method of comptime evaluation results in a
    // duplication of the sliced data in the final binary.  At some point, there
    // should be a more efficient method implemented for this, but it requires
    // far more complexity than it seems to be worth to solve at this point.
    // Comptimes are not addressable anyway, so the only observable difference
    // is in the size of the output binary.

    auto* array = evalComptime(node->ir_Slice.expr);

    ConstValue* value;
    if (array->kind == CONST_ARRAY) {
        auto start_index = 
            node->ir_Slice.start_index 
            ? evalComptimeIndexValue(node->ir_Slice.start_index, array->v_array.elems.size()) 
            : 0;

        auto end_index =
            node->ir_Slice.end_index
            ? evalComptimeIndexValue(node->ir_Slice.end_index, array->v_array.elems.size() + 1)
            : array->v_array.elems.size();

        if (start_index > end_index)
            comptimeEvalError(node->span, "lower slice index greater than upper slice index");

        value = allocComptime(CONST_ARRAY);
        value->v_array.elems = array->v_array.elems.subspan(start_index, end_index - start_index);
        value->v_array.elem_type = array->v_array.elem_type;
        value->v_array.mod_id = mod.id;
        value->v_array.alloc_loc = nullptr;
    } else if (array->kind == CONST_ZERO_ARRAY) {
        auto start_index = 
            node->ir_Slice.start_index 
            ? evalComptimeIndexValue(node->ir_Slice.start_index, array->v_zarr.num_elems) 
            : 0;

        auto end_index =
            node->ir_Slice.end_index
            ? evalComptimeIndexValue(node->ir_Slice.end_index, array->v_zarr.num_elems + 1)
            : array->v_zarr.num_elems;

        if (start_index > end_index)
            comptimeEvalError(node->span, "lower slice index greater than upper slice index");

        value = allocComptime(CONST_ZERO_ARRAY);
        value->v_zarr.num_elems = end_index - start_index;
        value->v_zarr.elem_type = array->v_zarr.elem_type;
        value->v_zarr.mod_id = mod.id;
        value->v_zarr.alloc_loc = nullptr;
    } else if (array->kind == CONST_STRING) {
         auto start_index = 
            node->ir_Slice.start_index 
            ? evalComptimeIndexValue(node->ir_Slice.start_index, array->v_str.value.size()) 
            : 0;

        auto end_index =
            node->ir_Slice.end_index
            ? evalComptimeIndexValue(node->ir_Slice.end_index, array->v_str.value.size() + 1)
            : array->v_zarr.num_elems;

        if (start_index > end_index)
            comptimeEvalError(node->span, "lower slice index greater than upper slice index");

        value = allocComptime(CONST_STRING);
        value->v_str.value = array->v_str.value.substr(start_index, end_index);
        value->v_str.mod_id = mod.id;
        value->v_str.alloc_loc = nullptr;
    } else {
        Panic("invalid comptime slice expr");
    }

    return value;
}

uint64_t Checker::evalComptimeIndexValue(HirExpr* node, uint64_t len) {
    uint64_t index;
    if (!evalComptimeSizeValue(node, &index)) {
        comptimeEvalError(node->span, "array index out of bounds");
    }

    if (index >= len) {
        comptimeEvalError(node->span, "array index out of bounds");
    }

    return index;
}

bool Checker::evalComptimeSizeValue(HirExpr* node, uint64_t* out_size) {
    auto* size_const = evalComptime(node);

    switch (size_const->kind) {
    case CONST_U8:
        *out_size = size_const->v_u8;
        break;
    case CONST_U16:
        *out_size = size_const->v_u16;
        break;
    case CONST_U32:
        *out_size = size_const->v_u32;
        break;
    case CONST_U64:
        *out_size = size_const->v_u64;
        break;
    case CONST_I8:
        if (size_const->v_i8 < 0)
            return false;

        *out_size = size_const->v_i8;
        break;
    case CONST_I16:
        if (size_const->v_i16 < 0)
            return false;

        *out_size = size_const->v_i16;
        break;
    case CONST_I32:
        if (size_const->v_i32 < 0)
            return false;

        *out_size = size_const->v_i32;
        break;
    case CONST_I64:
        if (size_const->v_i64 < 0)
            return false;

        *out_size = size_const->v_i64;
        break;
    }

    return true;
}

/* -------------------------------------------------------------------------- */

ConstValue* Checker::getComptimeNull(Type* type) {
    type = type->FullUnwrap();

    ConstValue* value;
    switch (type->kind) {
    case TYPE_INT: {
        auto& int_type = type->ty_Int;

        switch (int_type.bit_size) {
        case 8:
            value = allocComptime(CONST_I8);
            value->v_i8 = 0;
            break;
        case 16:
            value = allocComptime(CONST_I16);
            value->v_i16 = 0;
            break;
        case 32:
            value = allocComptime(CONST_I32);
            value->v_i32 = 0;
            break;
        case 64:
            value = allocComptime(CONST_I64);
            value->v_i64 = 0;
            break;
        }

        if (!int_type.is_signed) {
            value->kind = (ConstKind)((int)value->kind + 1);
        }
    } break;
    case TYPE_FLOAT:
        if (type->ty_Float.bit_size == 32) {
            value = allocComptime(CONST_F32);
            value->v_f32 = 0;
        } else {
            value = allocComptime(CONST_F64);
            value->v_f64 = 0;
        }
        break;
    case TYPE_BOOL:
        value = allocComptime(CONST_BOOL);
        value->v_bool = false;
        break;
    case TYPE_PTR:
        value = allocComptime(CONST_PTR);
        value->v_ptr = 0;
        break;
    case TYPE_FUNC:
        value = allocComptime(CONST_FUNC);
        value->v_func = nullptr;
        break;
    case TYPE_ARRAY:
        value = allocComptime(CONST_ZERO_ARRAY);
        value->v_zarr.num_elems = type->ty_Array.len;
        value->v_zarr.elem_type = type->ty_Array.elem_type;
        value->v_zarr.mod_id = mod.id;
        value->v_zarr.alloc_loc = nullptr;
        break;
    case TYPE_SLICE:
        value = allocComptime(CONST_ARRAY);
        value->v_array.elems = {};
        value->v_array.elem_type = type->ty_Slice.elem_type->Inner();
        value->v_array.mod_id = mod.id;
        value->v_array.alloc_loc = nullptr;
        break;
    case TYPE_STRING:
        value = allocComptime(CONST_STRING);
        value->v_str.value = "";
        value->v_str.mod_id = mod.id;
        value->v_str.alloc_loc = nullptr;
        break;
    case TYPE_STRUCT: {
        std::vector<ConstValue*> field_values;
        for (auto& field : type->ty_Struct.fields) {
            field_values.push_back(getComptimeNull(field.type));
        }

        value = allocComptime(CONST_STRUCT);
        value->v_struct.fields = arena.MoveVec(std::move(field_values));
        value->v_struct.mod_id = mod.id;
        value->v_struct.alloc_loc = nullptr;
    } break;
    default:
        Panic("comptime null not implemented for type {}", (int)type->kind);
        break;
    }

    return value;
}

static ConstValue size_ref_const {};

size_t const_variant_sizes[CONSTS_COUNT] = {
    sizeof(size_ref_const.v_i8),
    sizeof(size_ref_const.v_u8),
    sizeof(size_ref_const.v_i16),
    sizeof(size_ref_const.v_u16),
    sizeof(size_ref_const.v_i32),
    sizeof(size_ref_const.v_u32),
    sizeof(size_ref_const.v_i64),
    sizeof(size_ref_const.v_u64),
    sizeof(size_ref_const.v_f32),
    sizeof(size_ref_const.v_f64),
    sizeof(size_ref_const.v_bool),
    sizeof(size_ref_const.v_ptr),
    sizeof(size_ref_const.v_func),
    sizeof(size_ref_const.v_array),
    sizeof(size_ref_const.v_zarr),
    sizeof(size_ref_const.v_str),
    sizeof(size_ref_const.v_struct)
};
#define LARGEST_CONST_VARIANT ((sizeof(size_ref_const.v_struct)))

ConstValue* Checker::allocComptime(ConstKind kind) {
    size_t alloc_size = sizeof(ConstValue) - LARGEST_CONST_VARIANT + const_variant_sizes[kind];

    auto* value = (ConstValue*)arena.Alloc(alloc_size);
    value->kind = kind;
    return value;
}

void Checker::comptimeEvalError(const TextSpan& span, const std::string& message) {
    fatal(span, "evaluating compile-time expression: {}", message);
}
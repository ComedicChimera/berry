#include "codegen.hpp"

ConstValue* CodeGenerator::evalComptime(AstExpr* node) {
    ConstValue* value;

    switch (node->kind) {
    case AST_CAST:
        value = evalComptimeCast(node);
        break;
    case AST_BINOP:
        value = evalComptimeBinaryOp(node);
        break;
    case AST_UNOP:
        value = evalComptimeUnaryOp(node);
        break;
    case AST_INDEX: {
        auto* array = evalComptime(node->an_Index.array);

        if (array->kind == CONST_ARRAY) {
            auto index = evalComptimeIndexValue(node->an_Index.index, array->v_array.elems.size());

            value = array->v_array.elems[index];
        } else if (array->kind == CONST_ZERO_ARRAY) {
            evalComptimeIndexValue(node->an_Index.index, array->v_zarr.num_elems);

            value = getComptimeNull(array->v_zarr.elem_type);
        } else {
            Panic("invalid comptime index expr");
        }
    } break;
    case AST_SLICE:
        value = evalComptimeSlice(node);
        break;
    case AST_FIELD: {
        auto* root_value = evalComptime(node->an_Field.root);

        if (root_value->kind == CONST_ARRAY) {
            Assert(node->an_Field.field_name == "_len", "access to array._ptr in comptime expr");

            // TODO: platform sized integers
            value = allocComptime(CONST_I64);
            value->v_i64 = (int64_t)root_value->v_array.elems.size();
        } else if (root_value->kind == CONST_STRING) {
            Assert(node->an_Field.field_name == "_len", "access to string._ptr in comptime expr");

            // TODO: platform sized integers
            value = allocComptime(CONST_I64);
            value->v_i64 = (int64_t)root_value->v_str.value.size();
        } else {
            Assert(root_value->kind == CONST_STRUCT, "invalid comptime field access");
            value = root_value->v_struct.fields[node->an_Field.field_index];
        }
    } break;
    case AST_STATIC_GET: {
        auto* symbol = node->an_Field.imported_sym;
        Assert(symbol->flags & SYM_COMPTIME, "comptime eval with non-comptime symbol");

        if (symbol->flags & SYM_FUNC) {
            auto* value = allocComptime(CONST_FUNC);
            value->v_func = symbol;
        } else {
            auto* def = src_mod.deps[node->an_Field.root->an_Ident.dep_id].mod->defs[symbol->def_number];
            Assert(def->kind == AST_GLVAR && def->an_GlVar.const_value > CONST_VALUE_MARKER, "invalid comptime symbol");
            value = def->an_GlVar.const_value;
        }
    } break;
    case AST_NEW: {
        Assert(node->an_New.size_expr != nullptr, "new without fixed size is not comptime");

        uint64_t size;
        if (!evalComptimeSize(node->an_New.size_expr, &size)) {
            comptimeEvalError(node->an_New.size_expr->span, "negative array size");
        }
        
        value = allocComptime(CONST_ZERO_ARRAY);
        value->v_zarr.num_elems = size;
        value->v_zarr.elem_type = node->an_New.elem_type->Inner();
        value->v_zarr.mod_id = src_mod.id;
        value->v_zarr.alloc_loc = nullptr;
    } break;
    case AST_ARRAY: {
        value = allocComptime(CONST_ARRAY);

        std::vector<ConstValue*> elems;
        for (auto* ast_elem : node->an_Array.elems) {
            elems.push_back(evalComptime(ast_elem));
        }

        value->v_array.elems = arena.MoveVec(std::move(elems));
        value->v_array.elem_type = node->type->Inner()->ty_Array.elem_type->Inner();
        value->v_array.mod_id = src_mod.id;
        value->v_array.alloc_loc = nullptr;
    } break;
    case AST_STRUCT_LIT_POS:
        value = evalComptimeStructLitPos(node);
        break;
    case AST_STRUCT_LIT_NAMED: 
        value = evalComptimeStructLitNamed(node);
        break; 
    case AST_IDENT: {
        auto* symbol = node->an_Ident.symbol;
        Assert(symbol->flags & SYM_COMPTIME, "comptime eval with non-comptime symbol");

        if (symbol->flags & SYM_FUNC) {
            auto* value = allocComptime(CONST_FUNC);
            value->v_func = symbol;
        } else {
            auto* def = src_mod.defs[symbol->def_number];
            Assert(def->kind == AST_GLVAR && def->an_GlVar.const_value > CONST_VALUE_MARKER, "invalid comptime symbol");
            value = def->an_GlVar.const_value;
        }
    } break;
    case AST_INT: {
        auto& int_type = node->type->Inner()->ty_Int;

        switch (int_type.bit_size) {
        case 8:
            value = allocComptime(CONST_U8);
            value->v_u8 = (uint8_t)node->an_Int.value;
            break;
        case 16:
            value = allocComptime(CONST_U16);
            value->v_u16 = (uint16_t)node->an_Int.value;
            break;
        case 32:
            value = allocComptime(CONST_U32);
            value->v_u32 = (uint32_t)node->an_Int.value;
            break;
        case 64:
            value = allocComptime(CONST_U64);
            value->v_u64 = node->an_Int.value;
            break;
        }

        if (int_type.is_signed) {
            value->kind = (ConstKind)((int)value->kind - 1);
        }
    } break;
    case AST_FLOAT:
        if (node->type->Inner()->ty_Float.bit_size == 32) {
            value = allocComptime(CONST_F32);
            value->v_f32 = (float)node->an_Float.value;
        } else {
            value = allocComptime(CONST_F64);
            value->v_f64 = node->an_Float.value;
        }
        break;
    case AST_BOOL:
        value = allocComptime(CONST_BOOL);
        value->v_bool = node->an_Bool.value;
        break;
    case AST_STR:
        value = allocComptime(CONST_STRING);
        value->v_str.value = node->an_String.value;
        value->v_str.mod_id = src_mod.id;
        value->v_str.alloc_loc = nullptr;
        break;
    case AST_NULL:
        value = getComptimeNull(node->type);
        break;
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

ConstValue* CodeGenerator::evalComptimeCast(AstExpr* node) {
    auto* src = evalComptime(node->an_Cast.src);

    auto* dest_type = node->type->Inner();
    if (tctx.Equal(node->an_Cast.src->type, dest_type)) {
        return src;
    }

    ConstValue* value;
    switch (dest_type->kind) {
    case TYPE_BOOL:
        value = allocComptime(CONST_BOOL);
        COMPTIME_INT_CAST(v_bool, bool);
        break;
    case TYPE_INT:
        // TODO
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
            COMPTIME_INT_CAST(v_ptr, size_t);
        }
        break;
    case TYPE_ARRAY:
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
            value->v_array.mod_id = src_mod.id;
            value->v_array.alloc_loc = nullptr;
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

        value->v_str.mod_id = src_mod.id;
        value->v_str.alloc_loc = nullptr;
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

ConstValue* CodeGenerator::evalComptimeBinaryOp(AstExpr* node) {
    auto* lhs = evalComptime(node->an_Binop.lhs);

    if (node->an_Binop.op == AOP_LGAND) {
        if (lhs->kind == CONST_BOOL) {
            return lhs->v_bool ? evalComptime(node->an_Binop.rhs) : lhs;
        }
    } else if (node->an_Binop.op == AOP_LGOR) {
        if (lhs->kind == CONST_BOOL) {
            return lhs->v_bool ? lhs : evalComptime(node->an_Binop.rhs);
        }
    }

    auto* rhs = evalComptime(node->an_Binop.rhs);

    ConstValue* value = nullptr;
    switch (node->an_Binop.op) {
    case AOP_ADD:
        COMPTIME_NUM_BINOP(+);
        break;
    case AOP_SUB:
        COMPTIME_NUM_BINOP(-);
        break;
    case AOP_MUL:
        COMPTIME_NUM_BINOP(*);
        break;
    case AOP_DIV:
        if (checkComptimeNonzero(rhs)) {
            comptimeEvalError(node->an_Binop.rhs->span, "integer divide by zero");
        }
        COMPTIME_NUM_BINOP(/);
        break;
    case AOP_MOD:
        if (checkComptimeNonzero(rhs)) {
            comptimeEvalError(node->an_Binop.rhs->span, "integer divide by zero");
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
    case AOP_SHL:
        COMPTIME_INT_BINOP(<<);
        break;
    case AOP_SHR:
        COMPTIME_INT_BINOP(>>);
        break;
    case AOP_BWAND:
        COMPTIME_INT_BINOP(&);
        break;
    case AOP_BWOR:
        COMPTIME_INT_BINOP(|);
        break;
    case AOP_BWXOR:
        COMPTIME_INT_BINOP(^);
        break;
    case AOP_EQ:
        COMPTIME_COMP_NUM_BINOP(==);
        break;
    case AOP_NE:
        COMPTIME_COMP_NUM_BINOP(!=);
        break;
    case AOP_LT:
        COMPTIME_COMP_NUM_BINOP(<);
        break;
    case AOP_GT:
        COMPTIME_COMP_NUM_BINOP(>);
        break;
    case AOP_LE:
        COMPTIME_COMP_NUM_BINOP(<=);
        break;
    case AOP_GE:
        COMPTIME_COMP_NUM_BINOP(>=);
        break;
    }

    if (value == nullptr)
        Panic("unimplemented comptime binary op");

    return value;
}

ConstValue* CodeGenerator::evalComptimeUnaryOp(AstExpr* node) {
    auto* operand = evalComptime(node->an_Unop.operand);

    ConstValue* value = nullptr;;
    switch (node->an_Unop.op) {
    case AOP_NEG:
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
    case AOP_BWNEG:
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
    case AOP_NOT:
        if (operand->kind == CONST_BOOL) {
            value = allocComptime(CONST_BOOL);
            value->v_bool = !operand->v_bool;
        }

        break;
    }

    Assert(value != nullptr, "unimplemented comptime unary operator");
    return value;
}

ConstValue* CodeGenerator::evalComptimeStructLitPos(AstExpr* node) {
    auto* struct_type = node->an_StructLitPos.root->type->Inner();

    if (struct_type->kind == TYPE_NAMED) {
        struct_type = struct_type->ty_Named.type;
    }

    Assert(struct_type->kind == TYPE_STRUCT, "struct lit has non struct type root");

    std::vector<ConstValue*> field_values;
    for (auto& field : node->an_StructLitPos.field_inits) {
        field_values.push_back(evalComptime(field));
    }

    for (size_t i = field_values.size() - 1; i < struct_type->ty_Struct.fields.size(); i++) {
        field_values.push_back(getComptimeNull(struct_type->ty_Struct.fields[i].type));
    }

    auto* value = allocComptime(CONST_STRUCT);
    value->v_struct.fields = arena.MoveVec(std::move(field_values));
    value->v_struct.struct_type = struct_type;
    value->v_struct.mod_id = src_mod.id;
    value->v_struct.alloc_loc = nullptr;
    return value;
}

ConstValue* CodeGenerator::evalComptimeStructLitNamed(AstExpr* node) {
    auto* struct_type = node->an_StructLitPos.root->type->Inner();

    if (struct_type->kind == TYPE_NAMED) {
        struct_type = struct_type->ty_Named.type;
    }

    Assert(struct_type->kind == TYPE_STRUCT, "struct lit has non struct type root");

    std::vector<ConstValue*> field_values(struct_type->ty_Struct.fields.size(), nullptr);
    for (auto& field : node->an_StructLitNamed.field_inits) {
        field_values[field.field_index] = evalComptime(field.expr);
    }

    for (size_t i = 0; i < field_values.size(); i++) {
        if (field_values[i] == nullptr)
            field_values[i] = getComptimeNull(struct_type->ty_Struct.fields[i].type);
    }

    auto* value = allocComptime(CONST_STRUCT);
    value->v_struct.fields = arena.MoveVec(std::move(field_values));
    value->v_struct.struct_type = struct_type;
    value->v_struct.mod_id = src_mod.id;
    value->v_struct.alloc_loc = nullptr;
    return value;
}

ConstValue* CodeGenerator::evalComptimeSlice(AstExpr* node) {
    // NOTE: This particular method of comptime evaluation results in a
    // duplication of the sliced data in the final binary.  At some point, there
    // should be a more efficient method implemented for this, but it requires
    // far more complexity than it seems to be worth to solve at this point.
    // Comptimes are not addressable anyway, so the only observable difference
    // is in the size of the output binary.

    auto* array = evalComptime(node->an_Slice.array);

    ConstValue* value;
    if (array->kind == CONST_ARRAY) {
        auto start_index = 
            node->an_Slice.start_index 
            ? evalComptimeIndexValue(node->an_Slice.start_index, array->v_array.elems.size()) 
            : 0;

        auto end_index =
            node->an_Slice.end_index
            ? evalComptimeIndexValue(node->an_Slice.end_index, array->v_array.elems.size() + 1)
            : array->v_array.elems.size();

        if (start_index > end_index)
            comptimeEvalError(node->span, "lower slice index greater than upper slice index");

        value = allocComptime(CONST_ARRAY);
        value->v_array.elems = array->v_array.elems.subspan(start_index, end_index - start_index);
        value->v_array.elem_type = array->v_array.elem_type;
        value->v_array.mod_id = src_mod.id;
        value->v_array.alloc_loc = nullptr;
    } else if (array->kind == CONST_ZERO_ARRAY) {
        auto start_index = 
            node->an_Slice.start_index 
            ? evalComptimeIndexValue(node->an_Slice.start_index, array->v_zarr.num_elems) 
            : 0;

        auto end_index =
            node->an_Slice.end_index
            ? evalComptimeIndexValue(node->an_Slice.end_index, array->v_zarr.num_elems + 1)
            : array->v_zarr.num_elems;

        if (start_index > end_index)
            comptimeEvalError(node->span, "lower slice index greater than upper slice index");

        value = allocComptime(CONST_ZERO_ARRAY);
        value->v_zarr.num_elems = end_index - start_index;
        value->v_zarr.elem_type = array->v_zarr.elem_type;
        value->v_zarr.mod_id = src_mod.id;
        value->v_zarr.alloc_loc = nullptr;
    } else {
        Panic("invalid comptime slice expr");
    }

    return value;
}

uint64_t CodeGenerator::evalComptimeIndexValue(AstExpr* node, uint64_t len) {
    uint64_t index;
    if (!evalComptimeSize(node, &index)) {
        comptimeEvalError(node->span, "array index out of bounds");
    }

    if (index >= len) {
        comptimeEvalError(node->span, "array index out of bounds");
    }

    return index;
}

bool CodeGenerator::evalComptimeSize(AstExpr* node, uint64_t* out_size) {
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

void CodeGenerator::comptimeEvalError(const TextSpan& span, const std::string& message) {
    // TODO
}

/* -------------------------------------------------------------------------- */

ConstValue* CodeGenerator::getComptimeNull(Type* type) {
    type = type->Inner();

    auto* named_type = type;
    if (type->kind == TYPE_NAMED) {
        type = type->ty_Named.type;
    }

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
        value = allocComptime(CONST_ARRAY);
        value->v_array.elems = {};
        value->v_array.elem_type = type->ty_Array.elem_type->Inner();
        value->v_array.mod_id = src_mod.id;
        value->v_array.alloc_loc = nullptr;
        break;
    case TYPE_STRING:
        value = allocComptime(CONST_STRING);
        value->v_str.value = "";
        value->v_str.mod_id = src_mod.id;
        value->v_str.alloc_loc = nullptr;
        break;
    case TYPE_STRUCT: {
        std::vector<ConstValue*> field_values;
        for (auto& field : type->ty_Struct.fields) {
            field_values.push_back(getComptimeNull(field.type));
        }

        value = allocComptime(CONST_STRUCT);
        value->v_struct.fields = arena.MoveVec(std::move(field_values));
        value->v_struct.struct_type = named_type;
        value->v_struct.mod_id = src_mod.id;
        value->v_struct.alloc_loc = nullptr;
    } break;
    default:
        Panic("comptime null not implemented for type {}", (int)type->kind);
        break;
    }

    return value;
}

static ConstValue size_ref_const;

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

ConstValue* CodeGenerator::allocComptime(ConstKind kind) {
    size_t alloc_size = sizeof(ConstValue) - LARGEST_CONST_VARIANT + const_variant_sizes[kind];

    auto* value = (ConstValue*)arena.Alloc(alloc_size);
    value->kind = kind;
    return value;
}

/* -------------------------------------------------------------------------- */

llvm::Constant* CodeGenerator::genComptime(ConstValue* value, bool exported, bool inner) {
    switch (value->kind) {
    case CONST_I8: return makeLLVMIntLit(&prim_i8_type, value->v_i8);
    case CONST_U8: return makeLLVMIntLit(&prim_u8_type, value->v_u8);
    case CONST_I16: return makeLLVMIntLit(&prim_i16_type, value->v_i16);
    case CONST_U16: return makeLLVMIntLit(&prim_u16_type, value->v_u16);
    case CONST_I32: return makeLLVMIntLit(&prim_i32_type, value->v_i32);
    case CONST_U32: return makeLLVMIntLit(&prim_u32_type, value->v_u32);
    case CONST_I64: return makeLLVMIntLit(&prim_i64_type, value->v_i64);
    case CONST_U64: return makeLLVMIntLit(&prim_u64_type, value->v_u64);
    case CONST_F32: return makeLLVMFloatLit(&prim_f32_type, value->v_f32);
    case CONST_F64: return makeLLVMFloatLit(&prim_f64_type, value->v_f64);
    case CONST_BOOL: return makeLLVMIntLit(&prim_bool_type, value->v_bool);
    case CONST_PTR: 
        if (value->v_ptr == 0) {
            return llvm::ConstantPointerNull::get(llvm::PointerType::get(ctx, 0));
        } else {
            return llvm::ConstantExpr::getIntToPtr(
                // TODO: platform integer sizes
                makeLLVMIntLit(&prim_i64_type, value->v_ptr),
                llvm::PointerType::get(ctx, 0)
            );
        }
    case CONST_FUNC: {
        auto* sym = value->v_func;

        if (sym->parent_id == src_mod.id) {
            return llvm::dyn_cast<llvm::Constant>(sym->llvm_value);
        } else {
            size_t dep_id = 0;
            for (auto& dep : src_mod.deps) {
                if (dep.mod->id == sym->parent_id)
                    break;

                dep_id++;
            }

            return llvm::dyn_cast<llvm::Constant>(loaded_imports[dep_id][sym->def_number]);
        }
    } break;
    case CONST_ARRAY:
        return genComptimeArray(value, exported);
    case CONST_ZERO_ARRAY:
        return genComptimeZeroArray(value, exported);
    case CONST_STRING:
        return genComptimeString(value, exported);
    case CONST_STRUCT:
        return genComptimeStruct(value, exported, inner);
    default:
        Panic("unimplemented comptime value");
        break;
    }
}

static size_t const_id_counter = 0;

llvm::Constant* CodeGenerator::genComptimeArray(ConstValue* value, bool exported) {
    auto* ll_arr_data_type = llvm::ArrayType::get(
        genType(value->v_array.elem_type, true), 
        value->v_array.elems.size()
    );

    llvm::Constant* gv;
    if (value->v_array.alloc_loc == nullptr) {
        std::vector<llvm::Constant*> ll_elems;
        for (auto* elem : value->v_array.elems) {
            ll_elems.push_back(genComptime(elem, false, true));
        }

        auto* ll_array = llvm::ConstantArray::get(ll_arr_data_type, ll_elems);

        gv = new llvm::GlobalVariable(
            mod, 
            ll_arr_data_type, 
            true, 
            exported ? llvm::GlobalValue::ExternalLinkage : llvm::GlobalValue::PrivateLinkage, 
            ll_array, 
            std::format("__$const{}", const_id_counter++)
        );
    } else if (value->v_array.mod_id == src_mod.id) {
        gv = value->v_array.alloc_loc;
    } else {
        gv = new llvm::GlobalVariable(
            mod, 
            ll_arr_data_type, 
            true, 
            llvm::GlobalValue::ExternalLinkage, 
            nullptr, 
            value->v_array.alloc_loc->getName()
        );
    }

    value->v_array.alloc_loc = gv;
    value->v_array.mod_id = src_mod.id;
    return llvm::ConstantStruct::get(ll_array_type, { gv, getPlatformIntConst(value->v_array.elems.size()) });
}

llvm::Constant* CodeGenerator::genComptimeZeroArray(ConstValue* value, bool exported) {
    auto* ll_arr_data_type = llvm::ArrayType::get(
        genType(value->v_zarr.elem_type, true), 
        value->v_zarr.num_elems
    );

    llvm::Constant* gv;
    if (value->v_zarr.alloc_loc == nullptr) {
        gv = new llvm::GlobalVariable(
            mod, 
            ll_arr_data_type, 
            true, 
            exported ? llvm::GlobalValue::ExternalLinkage : llvm::GlobalValue::PrivateLinkage, 
            getNullValue(ll_arr_data_type), 
            std::format("__$const{}", const_id_counter++)
        );
    } else if (value->v_zarr.mod_id == src_mod.id) {
        gv = value->v_zarr.alloc_loc;
    } else {
        gv = new llvm::GlobalVariable(
            mod, 
            ll_arr_data_type, 
            true, 
            llvm::GlobalValue::ExternalLinkage, 
            nullptr, 
            value->v_zarr.alloc_loc->getName()
        );
    }

    value->v_zarr.alloc_loc = gv;
    value->v_zarr.mod_id = src_mod.id;
    return llvm::ConstantStruct::get(ll_array_type, { gv, getPlatformIntConst(value->v_zarr.num_elems) });
}

llvm::Constant* CodeGenerator::genComptimeString(ConstValue* value, bool exported) {
    auto* ll_string_type = llvm::ArrayType::get(
        llvm::Type::getInt8Ty(ctx),
        value->v_str.value.size()
    );

    llvm::Constant* gv;
    
    if (value->v_str.alloc_loc == nullptr) {
        auto decoded = decodeStrLit(value->v_str.value);
        gv = llvm::ConstantDataArray::getString(ctx, decoded, false);

        if (exported) {
            auto* str_gv = dyn_cast<llvm::GlobalValue>(gv);
            str_gv->setLinkage(llvm::GlobalValue::ExternalLinkage);
            str_gv->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::None);
        }
    } else if (value->v_str.mod_id == src_mod.id) {
        gv = value->v_str.alloc_loc;
    } else {
        gv = new llvm::GlobalVariable(
            mod,
            ll_string_type,
            true, 
            llvm::GlobalValue::ExternalLinkage, 
            nullptr, 
            value->v_zarr.alloc_loc->getName()
        );
    }

    value->v_str.alloc_loc = gv;
    value->v_str.mod_id = src_mod.id;
    return llvm::ConstantStruct::get(ll_array_type, { gv, getPlatformIntConst(value->v_str.value.size()) });
}

llvm::Constant* CodeGenerator::genComptimeStruct(ConstValue* value, bool exported, bool inner) {
    if (inner || !shouldPtrWrap(value->v_struct.struct_type)) {
        return genInnerComptimeStruct(value, exported);
    }

    llvm::Constant* gv;
    if (value->v_struct.alloc_loc == nullptr) {
        auto* struct_const = genInnerComptimeStruct(value, exported);
        gv = new llvm::GlobalVariable(
            mod,
            struct_const->getType(),
            true,
            exported ? llvm::GlobalValue::ExternalLinkage : llvm::GlobalValue::PrivateLinkage,
            struct_const,
            std::format("__$const{}", const_id_counter++)
        );
    } else if (value->v_struct.mod_id == src_mod.id) {
        gv = value->v_struct.alloc_loc;
    } else {
        gv = new llvm::GlobalVariable(
            mod,
            genType(value->v_struct.struct_type, true),
            true,
            llvm::GlobalValue::ExternalLinkage,
            nullptr,
            value->v_struct.alloc_loc->getName()
        );
    }

    value->v_struct.alloc_loc = gv;
    value->v_struct.mod_id = src_mod.id;
    return gv;
}

llvm::Constant* CodeGenerator::genInnerComptimeStruct(ConstValue* value, bool exported) {
    std::vector<llvm::Constant*> field_values;  
    for (auto* field : value->v_struct.fields) {
        field_values.push_back(genComptime(field, exported, true));
    }

    return llvm::ConstantStruct::get(
        dyn_cast<llvm::StructType>(genType(value->v_struct.struct_type, true)), 
        field_values
    );
}

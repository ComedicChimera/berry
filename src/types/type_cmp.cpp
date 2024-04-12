#include "types.hpp"

bool TypeContext::innerEqual(Type* a, Type* b) {
    if (a->kind == TYPE_UNTYP) {
        if (b->kind == TYPE_UNTYP) {
            return tryUnion(a->ty_Untyp.key, b->ty_Untyp.key);
        }

        return tryConcrete(a->ty_Untyp.key, b);
    } else if (b->kind == TYPE_UNTYP) {
        return tryConcrete(b->ty_Untyp.key, a);
    }

    switch (a->kind) {
    case TYPE_INT:
        if (b->kind == TYPE_INT) {
            return a->ty_Int.bit_size == b->ty_Int.bit_size && a->ty_Int.is_signed == b->ty_Int.is_signed;
        }
        break;
    case TYPE_FLOAT:
        if (b->kind == TYPE_FLOAT) {
            return a->ty_Float.bit_size == b->ty_Float.bit_size;
        }
        break;
    case TYPE_BOOL:
        return b->kind == TYPE_BOOL;
    case TYPE_UNIT:
        return b->kind == TYPE_UNIT;
    case TYPE_STRING:
        return b->kind == TYPE_STRING;
    case TYPE_SLICE:
        if (b->kind == TYPE_SLICE) {
            return Equal(a->ty_Slice.elem_type, b->ty_Slice.elem_type);
        }
        break;
    case TYPE_ARRAY:
        if (b->kind == TYPE_ARRAY) {
            return a->ty_Array.len == b->ty_Array.len && Equal(a->ty_Array.elem_type, b->ty_Array.elem_type);
        }
        break;
    case TYPE_PTR:
        if (b->kind == TYPE_PTR) {
            return Equal(a->ty_Ptr.elem_type, b->ty_Ptr.elem_type);
        }
        break;
    case TYPE_FUNC:
        if (b->kind == TYPE_FUNC) {
            auto& aft = a->ty_Func;
            auto& bft = b->ty_Func;

            if (aft.param_types.size() != bft.param_types.size()) {
                return false;
            }

            for (int i = 0; i < aft.param_types.size(); i++) {
                if (!Equal(aft.param_types[i], bft.param_types[i])) {
                    return false;
                }
            }


            return Equal(aft.return_type, bft.return_type);
        }
        break;
    case TYPE_NAMED:
        if (b->kind == TYPE_NAMED) {
            return a->ty_Named.mod_id == b->ty_Named.mod_id && a->ty_Named.name == b->ty_Named.name;
        }
        break;
    case TYPE_STRUCT:
        if (b->kind == TYPE_STRUCT) {
            auto& ast = a->ty_Struct;
            auto& bst = b->ty_Struct;

            if (ast.fields.size() != bst.fields.size()) {
                return false;
            }

            for (size_t i = 0; i < ast.fields.size(); i++) {
                if (ast.fields[i].name != bst.fields[i].name)
                    return false;

                if (!Equal(ast.fields[i].type, bst.fields[i].type)) {
                    return false;
                }
            }

            return true;
        }
        break;
    default:
        Panic("type comparison not implemented for {}", (int)a->kind);
    }

    return false;
}

/* -------------------------------------------------------------------------- */

TypeConvResult TypeContext::innerSubType(Type* sub, Type* super) {
    if (sub->kind == TYPE_ARRAY && (super->kind == TYPE_SLICE || super->kind == TYPE_STRING)) {
        return Equal(super->ty_Slice.elem_type, sub->ty_Array.elem_type) ? TY_CONV_CAST : TY_CONV_FAIL;
    }

    return innerEqual(sub, super) ? TY_CONV_EQ : TY_CONV_FAIL;
}

/* -------------------------------------------------------------------------- */

bool TypeContext::innerCast(Type* src, Type* dest) {
    if (src->kind == TYPE_UNTYP) {        
        if (innerIsNumberType(dest)) {
            // We don't care if the types are incompatible: this mechanism just
            // allows us to optimize out the cast as necessary.
            tryConcrete(src->ty_Untyp.key, dest);

            return true;
        } else if (dest->kind == TYPE_BOOL || dest->kind == TYPE_PTR) {
            auto& entry = find(src->ty_Untyp.key);

            switch (entry.kind) {
            case UK_INT:
                return true;
            case UK_NUM:
                if (flags & TC_INFER) {
                    entry.kind = UK_INT;
                }

                // Because casts are always "terminal" expression, we know we
                // can always convert UK_NUM to integer if necessary: it will
                // never be later narrowed to float.
                return true;
            }
        }
        
        return false;
    }

    if (dest->kind == TYPE_NAMED) {
        if (src->kind != TYPE_NAMED) {
            return Cast(src, dest->ty_Named.type);
        }
    } else if (src->kind == TYPE_NAMED) {
        return Cast(src->ty_Named.type, dest);
    }

    switch (src->kind) {
    case TYPE_INT:
        if ((flags & TC_UNSAFE) && (dest->kind == TYPE_PTR || dest->kind == TYPE_ENUM))
            return true;
        
        return innerIsNumberType(dest) || dest->kind == TYPE_BOOL;
    case TYPE_FLOAT:
        return innerIsNumberType(dest);
    case TYPE_BOOL:
        if (dest->kind == TYPE_INT) {
            return true;
        }
        break;
    case TYPE_PTR:
        if ((flags & TC_UNSAFE) && (dest->kind == TYPE_INT || dest->kind == TYPE_PTR)) {
            return true;
        }
        break;
    case TYPE_SLICE:
        if (dest->kind == TYPE_STRING) {
            return Equal(src->ty_Slice.elem_type, &prim_u8_type);
        } else if ((flags & TC_UNSAFE) && dest->kind == TYPE_ARRAY) {
            return Equal(src->ty_Slice.elem_type, dest->ty_Array.elem_type);
        }
        break;
    case TYPE_STRING:
        if (dest->kind == TYPE_SLICE) {
            return Equal(&prim_u8_type, dest->ty_Slice.elem_type);
        } else if ((flags & TC_UNSAFE) && dest->kind == TYPE_ARRAY) {
            return Equal(&prim_u8_type, dest->ty_Array.elem_type);
        }
        break;
    case TYPE_ENUM:
        return dest->kind == TYPE_INT;
    }

    return innerSubType(src, dest) != TY_CONV_FAIL;
}
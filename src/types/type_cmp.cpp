#include "types.hpp"

bool TypeContext::Equal(Type* a, Type* b) {
    return innerEqual(a->Inner(), b->Inner());
}

bool TypeContext::innerEqual(Type* a, Type* b) {
    if (a->GetKind() == TYPE_UNTYP) {
        auto* a_ut = dynamic_cast<Untyped*>(a);

        if (b->GetKind() == TYPE_UNTYP) {
            auto* b_ut = dynamic_cast<Untyped*>(b);

            return tryUnion(a_ut->key, b_ut->key);
        }

        return tryConcrete(a_ut->key, b);
    } else if (b->GetKind() == TYPE_UNTYP) {
        auto* b_ut = dynamic_cast<Untyped*>(b);

        return tryConcrete(b_ut->key, a);
    }

    return a->impl_Equal(this, b);
}

/* -------------------------------------------------------------------------- */

bool TypeContext::SubType(Type* sub, Type* super) {
    return innerSubType(sub->Inner(), super->Inner());
}

bool TypeContext::innerSubType(Type* sub, Type* super) {
    if (super->impl_SuperType(this, sub))
        return true;

    return innerEqual(sub, super);
}

/* -------------------------------------------------------------------------- */

bool TypeContext::Cast(Type* src, Type* dest) {
    return innerCast(src->Inner(), dest->Inner());
}

bool TypeContext::innerCast(Type* src, Type* dest) {
    if (src->GetKind() == TYPE_UNTYP) {
        auto src_ut = dynamic_cast<Untyped*>(src);
        
        if (innerIsNumberType(dest)) {
            // We don't care if the types are incompatible: this mechanism just
            // allows us to optimize out the cast as necessary.
            tryConcrete(src_ut->key, dest);

            return true;
        } else if (dest->GetKind() == TYPE_BOOL || dest->GetKind() == TYPE_PTR) {
            auto& entry = find(src_ut->key);

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
            default:
                return false;
            }
        }
        
        return false;
    }

    if (src->impl_Cast(this, dest))
        return true;

    return innerSubType(src, dest);
}

/* -------------------------------------------------------------------------- */

Type* Type::Inner() {
    return this;
}

// By default, types have no subtyping relation (ex: int is not an explicit
// super type of anything).
bool Type::impl_SuperType(TypeContext *tctx, Type *sub) {
    return false;
}

// By default, types have no extra casting logic (ex: func types don't cast at
// all).
bool Type::impl_Cast(TypeContext *tctx, Type *dest) {
    return false;
}

/* -------------------------------------------------------------------------- */

bool IntType::impl_Equal(TypeContext* tctx, Type* other) {
    if (other->GetKind() == TYPE_INT) {
        auto o_it = dynamic_cast<IntType*>(other);

        return bit_size == o_it->bit_size && is_signed == o_it->is_signed;
    }

    return false;
}

bool FloatType::impl_Equal(TypeContext* tctx, Type* other) {
    if (other->GetKind() == TYPE_FLOAT) {
        auto o_ft = dynamic_cast<FloatType*>(other);

        return bit_size == o_ft->bit_size;
    }

    return false;
}

bool BoolType::impl_Equal(TypeContext* tctx, Type* other) {
    return other->GetKind() == TYPE_BOOL;
}

bool UnitType::impl_Equal(TypeContext* tctx, Type* other) {
    return other->GetKind() == TYPE_UNIT;
}

bool PointerType::impl_Equal(TypeContext* tctx, Type* other) {
    if (other->GetKind() == TYPE_PTR) {
        auto o_pt = dynamic_cast<PointerType*>(other);

        if (immut != o_pt->immut)
            return false;

        return tctx->Equal(elem_type, o_pt->elem_type);
    }

    return false;
}

bool FuncType::impl_Equal(TypeContext* tctx, Type* other) {
    if (other->GetKind() == TYPE_FUNC) {
        auto o_ft = dynamic_cast<FuncType*>(other);

        if (param_types.size() != o_ft->param_types.size())
            return false;

        for (int i = 0; i < param_types.size(); i++) {
            if (!tctx->Equal(param_types[i], o_ft->param_types[i]))
                return false;
        }

        return tctx->Equal(return_type, o_ft->return_type);
    }

    return false;
}

bool ArrayType::impl_Equal(TypeContext* tctx, Type *other) {
    if (other->GetKind() == TYPE_ARRAY) {
        auto o_at = dynamic_cast<ArrayType*>(other);

        return tctx->Equal(elem_type, o_at->elem_type);
    }

    return false;
}

/* -------------------------------------------------------------------------- */

bool IntType::impl_Cast(TypeContext* tctx, Type* dest) {
    switch (dest->GetKind()) {
    case TYPE_BOOL: case TYPE_INT: case TYPE_FLOAT: case TYPE_PTR:
        return true;
    default:
        return false;
    }
}

bool FloatType::impl_Cast(TypeContext* tctx, Type* dest) {
    return innerIsNumberType(dest);
}

bool BoolType::impl_Cast(TypeContext* tctx, Type* dest) {
    return dest->GetKind() == TYPE_INT;
}

bool PointerType::impl_Cast(TypeContext* tctx, Type* dest) {
    return dest->GetKind() == TYPE_PTR || dest->GetKind() == TYPE_INT;
}
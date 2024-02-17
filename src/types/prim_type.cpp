#include "types.hpp"

bool TypeContext::IsNumberType(Type* type) {
    auto* inner = type->Inner();

    if (inner->kind == TYPE_UNTYP) {
        return true;
    }

    return innerIsNumberType(inner);
}

bool TypeContext::IsIntType(Type* type) {
    auto* inner = type->Inner();

    if (inner->kind == TYPE_UNTYP) {
        auto& entry = find(inner->ty_Untyp.key);

        if (entry.kind == UK_INT) {
            return true;
        } else if (entry.kind == UK_NUM) {
            if (flags & TC_INFER) {
                entry.kind = UK_INT;
                return true;
            }
        }

        return false;
    }

    return inner->kind == TYPE_INT;
}
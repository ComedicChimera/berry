#include "types.hpp"

bool TypeContext::IsNumberType(Type* type) {
    auto* inner = type->Inner();

    if (inner->GetKind() == TYPE_UNTYP) {
        return true;
    }

    return innerIsNumberType(inner);
}

bool TypeContext::IsIntType(Type* type) {
    auto* inner = type->Inner();

    if (inner->GetKind() == TYPE_UNTYP) {
        auto* uk = dynamic_cast<Untyped*>(inner);

        auto& entry = find(uk->key);

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

    return inner->GetKind() == TYPE_INT;
}
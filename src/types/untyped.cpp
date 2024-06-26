#include "types.hpp"

Type* Type::Inner() {
    if (kind == TYPE_UNTYP) {
        if (ty_Untyp.concrete_type != nullptr) {
            return ty_Untyp.concrete_type;
        }

        auto* ctype = ty_Untyp.parent->getConcreteType(this);
        if (ctype == nullptr)
            return this;

        return ctype;
    } else if (kind == TYPE_ALIAS) {
        return ty_Named.type->Inner();
    }

    return this;
}

Type* Type::FullUnwrap() {
    auto* inner_type = Inner();

    if (inner_type->kind == TYPE_NAMED)
        return inner_type->ty_Named.type;

    return inner_type;
}

/* -------------------------------------------------------------------------- */

void TypeContext::AddUntyped(Type* ut, UntypedKind kind) {
    size_t key = unt_uf.size();
    ut->ty_Untyp.key = key;
    ut->ty_Untyp.parent = this;
    ut->ty_Untyp.concrete_type = nullptr;

    unt_uf.push_back(ut);
    unt_table.emplace_back(untypedTableEntry{ key, kind, nullptr });
}

std::string TypeContext::untypedToString(const Type* ut) {
    auto& entry = find(ut->ty_Untyp.key);

    if (entry.concrete_type == nullptr) {
        switch (entry.kind) {
        case UK_INT:
            return "untyped int";
        case UK_FLOAT:
            return "untyped float";
        case UK_NUM:
            return "untyped number";
        case UK_NULL:
            return "untyped null";
        default:
            Panic("invalid untyped kind");
            return {};
        }
    }

    return entry.concrete_type->ToString();
}

Type* TypeContext::getConcreteType(const Type* ut) {
    auto& entry = find(ut->ty_Untyp.key);
    return entry.concrete_type;
}

void TypeContext::InferAll() {
    for (auto& ut : unt_uf) {
        auto& entry = find(ut->ty_Untyp.key);
        if (entry.concrete_type == nullptr) {
            switch (entry.kind) {
            case UK_INT: case UK_NUM:
                ut->ty_Untyp.concrete_type = platform_int_type;
                break;
            case UK_FLOAT:
                ut->ty_Untyp.concrete_type = &prim_f64_type;
                break;
            }
        } else {
            ut->ty_Untyp.concrete_type = entry.concrete_type;
        }
    }
}

void TypeContext::Clear() {
    infer_enabled = false;
    unsafe_enabled = false;

    unt_table.clear();
    unt_uf.clear();
}

/* -------------------------------------------------------------------------- */

bool TypeContext::tryConcrete(size_t key, Type* other) {
    auto& entry = find(key);

    bool compat = false;
    switch (entry.kind) {
    case UK_FLOAT:
        compat = other->kind == TYPE_FLOAT;
        break;
    case UK_INT:
        compat = other->kind == TYPE_INT;
        break;
    case UK_NUM:
        compat = innerIsNumberType(other);
        break;
    case UK_NULL:
        compat = true;
        break;
    }

    if (compat && infer_enabled) {
        entry.concrete_type = other;
    }

    return compat;
}

TypeContext::untypedTableEntry& TypeContext::find(size_t key) {
    while (key != unt_uf[key]->ty_Untyp.key) {
        key = unt_uf[key]->ty_Untyp.key;
    }

    return unt_table[key];
}

TypeContext::untypedTableEntry& TypeContext::find(size_t key, int* rank) {
    *rank = 0;

    while (key != unt_uf[key]->ty_Untyp.key) {
        key = unt_uf[key]->ty_Untyp.key;
        (*rank)++;
    }

    return unt_table[key];
}

bool TypeContext::tryUnion(size_t a, size_t b) {
    int arank, brank;
    auto& aentry = find(a, &arank);
    auto& bentry = find(b, &brank);

    int which;
    if (aentry.kind == UK_NULL) {
        which = 1;
    } else if (bentry.kind == UK_NULL) {
        which = 0;
    } else if (aentry.kind == UK_NUM) {
        which = 1;
    } else if (bentry.kind == UK_NUM) {
        which = 0;
    } else if (aentry.kind == bentry.kind) {
        which = 0;
    } else {
        return false;
    }

    if (!infer_enabled)
        return true;

    auto& dominant = which ? bentry : aentry;

    if (arank > brank) {
        unt_uf[bentry.key]->ty_Untyp.key = aentry.key;

        unt_table[aentry.key] = dominant;
    } else {
        unt_uf[aentry.key]->ty_Untyp.key = bentry.key;

        unt_table[bentry.key] = dominant;
    }

    return true;
}
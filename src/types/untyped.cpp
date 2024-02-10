#include "types.hpp"

Untyped::Untyped(TypeContext* parent, UntypedKind kind)
: parent(parent)
, concrete_type(nullptr)
{
    parent->AddUntyped(this, kind);
}

std::string Untyped::ToString() const {
    if (concrete_type != nullptr) {
        return concrete_type->ToString();
    } else {
        return parent->GetAbstractUntypedStr(this);
    }
}

Type* Untyped::Inner() {
    if (concrete_type != nullptr) {
        return concrete_type;
    } 

    auto* ctype = parent->GetConcreteType(this);
    if (ctype == nullptr)
        return this;

    return ctype;
}

bool Untyped::impl_Equal(TypeContext* tctx, Type* other) {
    // Should never be called directly.
    Panic("call to impl_Equal for untyped");
    return false;
}

/* -------------------------------------------------------------------------- */

void TypeContext::AddUntyped(Untyped* ut, UntypedKind kind) {
    uint64_t key = unt_uf.size();
    ut->key = key;

    unt_uf.push_back(ut);
    unt_table.emplace(key, untypedTableEntry{ key, kind, nullptr });
}

std::string TypeContext::GetAbstractUntypedStr(const Untyped* ut) {
    auto& entry = find(ut->key);

    if (entry.concrete_type == nullptr) {
        switch (entry.kind) {
        case UK_INT:
            return "untyped int";
        case UK_FLOAT:
            return "untyped float";
        case UK_NUM:
            return "untyped number";
        default:
            Panic("invalid untyped kind");
            return {};
        }
    }

    return entry.concrete_type->ToString();
}

Type* TypeContext::GetConcreteType(Untyped* ut) {
    auto& entry = find(ut->key);
    return entry.concrete_type;
}

void TypeContext::InferAll() {
    for (auto& ut : unt_uf) {
        auto& entry = find(ut->key);
        if (entry.concrete_type == nullptr) {
            switch (entry.kind) {
            case UK_INT: case UK_NUM:
                ut->concrete_type = &prim_i64_type;
                break;
            case UK_FLOAT:
                ut->concrete_type = &prim_f64_type;
                break;
            }
        } else {
            ut->concrete_type = entry.concrete_type;
        }
    }
}

void TypeContext::Clear() {
    flags = TC_DEFAULT;

    unt_table.clear();
    unt_uf.clear();
}

/* -------------------------------------------------------------------------- */

bool TypeContext::tryConcrete(uint64_t key, Type* other) {
    auto& entry = find(key);

    bool compat = false;
    switch (entry.kind) {
    case UK_FLOAT:
        compat = other->GetKind() == TYPE_FLOAT;
        break;
    case UK_INT:
        compat = other->GetKind() == TYPE_INT;
        break;
    case UK_NUM:
        compat = innerIsNumberType(other);
        break;
    }

    if (compat && (flags & TC_INFER)) {
        entry.concrete_type = other;
    }

    return compat;
}

TypeContext::untypedTableEntry& TypeContext::find(uint64_t key) {
    while (key != unt_uf[key]->key) {
        key = unt_uf[key]->key;
    }

    return unt_table[key];
}

TypeContext::untypedTableEntry& TypeContext::find(uint64_t key, int* rank) {
    *rank = 0;

    while (key != unt_uf[key]->key) {
        key = unt_uf[key]->key;
        (*rank)++;
    }

    return unt_table[key];
}

bool TypeContext::tryUnion(uint64_t a, uint64_t b) {
    int arank, brank;
    auto& aentry = find(a, &arank);
    auto& bentry = find(b, &brank);

    int which;
    if (aentry.kind == UK_NUM) {
        which = 1;
    } else if (bentry.kind == UK_NUM) {
        which = 0;
    } else if (aentry.kind == bentry.kind) {
        which = 0;
    } else {
        return false;
    }

    if (flags & TC_INFER == 0)
        return true;

    auto& dominant = which ? bentry : aentry;

    if (arank > brank) {
        unt_uf[bentry.key]->key = aentry.key;

        unt_table[aentry.key] = dominant;
        unt_table.erase(bentry.key);
    } else {
        unt_uf[aentry.key]->key = bentry.key;

        unt_table[bentry.key] = dominant;
        unt_table.erase(aentry.key);
    }

    return true;
}
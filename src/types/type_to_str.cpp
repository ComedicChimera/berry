#include "types.hpp"

std::string IntType::ToString() const {
    return std::format("{}{}", is_signed ? 'i' : 'u', bit_size);
}

std::string FloatType::ToString() const {
    return std::format("f{}", bit_size);
}

std::string BoolType::ToString() const {
    return "bool";
}

std::string UnitType::ToString() const {
    return "unit";
}

std::string PointerType::ToString() const {
    if (immut) {
        return std::format("*const {}", elem_type->ToString());
    } else {
        return std::format("*{}", elem_type->ToString());
    }
}

std::string FuncType::ToString() const {
    std::string sb;

    if (param_types.size() == 0) {
        sb.append("()");
    } else if (param_types.size() == 1) {
        sb.append(param_types[0]->ToString());
    } else {
        sb.push_back('(');

        for (int i = 0; i < param_types.size(); i++) {
            if (i > 0) sb.append(", ");

            sb.append(param_types[i]->ToString());
        }

        sb.push_back(')');
    }

    sb.append(" -> ");
    sb.append(return_type->ToString());

    return sb;
}

std::string ArrayType::ToString() const {
    return std::format("[]{}", elem_type->ToString());
}
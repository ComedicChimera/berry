#include "types.hpp"

std::string Type::ToString() const {
    switch (kind) {
    case TYPE_INT:
        return std::format("{}{}", ty_Int.is_signed ? 'i' : 'u', ty_Int.bit_size);
    case TYPE_FLOAT:
        return std::format("f{}", ty_Float.bit_size);
    case TYPE_BOOL:
        return "bool";
    case TYPE_UNIT:
        return "unit";
    case TYPE_PTR:
        return std::format("*{}", ty_Ptr.elem_type->ToString());
    case TYPE_FUNC:
    {
        auto& ft = ty_Func;
        std::string sb;

        if (ft.param_types.size() == 0) {
            sb.append("()");
        } else if (ft.param_types.size() == 1) {
            sb.append(ft.param_types[0]->ToString());
        } else {
            sb.push_back('(');

            for (int i = 0; i < ft.param_types.size(); i++) {
                if (i > 0) sb.append(", ");

                sb.append(ft.param_types[i]->ToString());
            }

            sb.push_back(')');
        }

        sb.append(" -> ");
        sb.append(ft.return_type->ToString());

        return sb;
    }
    case TYPE_ARRAY:
        return std::format("[]{}", ty_Array.elem_type->ToString());
    case TYPE_UNTYP:
        if (ty_Untyp.concrete_type == nullptr) {
            return ty_Untyp.parent->untypedToString(this);
        }
        
        return ty_Untyp.concrete_type->ToString();
    default:
        Panic("string printing not implemented for {}", (int)kind);
    }
}
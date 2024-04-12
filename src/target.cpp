#include "target.hpp"

#include "llvm/IR/Type.h"

static TargetPlatform target_platform;

TargetPlatform& GetTargetPlatform() {
    return target_platform;
}

/* -------------------------------------------------------------------------- */

uint64_t TargetPlatform::GetComptimeSizeOf(Type* type) {
    return ll_layout->getTypeAllocSize(getLLVMType(type));
}

uint64_t TargetPlatform::GetComptimeAlignOf(Type* type) {
    return ll_layout->getPrefTypeAlign(getLLVMType(type)).value();
}

/* -------------------------------------------------------------------------- */

llvm::Type* TargetPlatform::getLLVMType(Type* type) {
    type = type->FullUnwrap();

    switch (type->kind) {
    case TYPE_INT:
        return llvm::IntegerType::get(ll_context, type->ty_Int.bit_size);
    case TYPE_FLOAT:
        if (type->ty_Float.bit_size == 64) {
            return llvm::Type::getDoubleTy(ll_context);
        } else {
            return llvm::Type::getFloatTy(ll_context);
        }
        break;
    case TYPE_BOOL:
    case TYPE_UNIT:
        return llvm::Type::getInt1Ty(ll_context);
    case TYPE_PTR:
    case TYPE_FUNC:
        return llvm::PointerType::get(ll_context, 0);
    case TYPE_ARRAY: {
        auto* ll_elem_type = getLLVMType(type->ty_Array.elem_type);
        return llvm::ArrayType::get(ll_elem_type, type->ty_Array.len);
    } break;
    case TYPE_SLICE:
    case TYPE_STRING:
        return llvm::StructType::get(ll_context, { 
            llvm::PointerType::get(ll_context, 0),
            getLLVMType(platform_int_type)   
        });
    case TYPE_STRUCT: {
        std::vector<llvm::Type*> ll_field_types;
        for (auto& field : type->ty_Struct.fields) {
            ll_field_types.push_back(getLLVMType(field.type));
        }

        return llvm::StructType::get(ll_context, ll_field_types);
    } break;
    case TYPE_ENUM:
        return getLLVMType(platform_int_type);
    default:
        Panic("cannot compute size or align of non-concrete type");
        break;
    }
}
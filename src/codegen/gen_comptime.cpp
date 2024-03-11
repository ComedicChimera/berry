#include "codegen.hpp"

ConstValue* CodeGenerator::getComptimeNull(Type* type) {
    type = type->Inner();

    if (type->kind == TYPE_NAMED) {
        type = type->ty_Named.type;
    }

    switch (type->kind) {
    case TYPE_INT:
    case TYPE_FLOAT:
    case TYPE_BOOL:
    case TYPE_PTR:
    case TYPE_FUNC:
    case TYPE_STRING:
    case TYPE_ARRAY:
    case TYPE_STRUCT:
    default:
        Panic("comptime null not implemented for type {}", (int)type->kind);
        break;
    }
}

ConstValue* CodeGenerator::evalComptime(AstExpr* node) {

}

/* -------------------------------------------------------------------------- */

llvm::Constant* CodeGenerator::genComptime(ConstValue* value, AstAllocMode alloc_mode) {

}

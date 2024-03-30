#include "ast.hpp"

bool AstExpr::IsLValue() const {
    switch (kind) {
    case AST_IDENT:
        Assert(an_Ident.symbol != nullptr, "call to IsLValue on identifier with null symbol");
        return !immut;
    case AST_DEREF:
        return true;
    case AST_INDEX:
        return an_Index.array->IsLValue() && !immut;
    case AST_FIELD:
        return an_Field.root->type->Inner()->kind == TYPE_PTR || an_Field.root->IsLValue();
    case AST_STATIC_GET:
        return !an_Field.imported_sym->immut;
    }

    return false;
}

#include "ast.hpp"

bool AstExpr::IsLValue() const {
    switch (kind) {
    case AST_IDENT:
    case AST_DEREF:
        return true;
    case AST_INDEX:
        return an_Index.array->IsLValue();
    case AST_FIELD:
        return an_Field.root->IsLValue();
    }

    return false;
}
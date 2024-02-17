#include "ast.hpp"

bool AstExpr::IsLValue() const {
    switch (kind) {
    case AST_IDENT:
    case AST_DEREF:
    case AST_INDEX:
    case AST_SLICE:
        return true;
    }

    return false;
}
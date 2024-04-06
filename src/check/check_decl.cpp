#include "checker.hpp"

void Checker::checkDecl(size_t decl_number, Module::Decl& decl) {
    switch (decl.ast_decl->kind) {
    case AST_FUNC:
        // TODO: check attributes
        decl.hir_decl = checkFuncDecl(decl_number, decl.ast_decl);
    case AST_VAR:
        // TODO: check attributes
        decl.hir_decl = checkGlobalVarDecl(decl_number, decl.ast_decl);
    case AST_CONST:
        // TODO: check attributes
    case AST_TYPEDEF:
    default:
        Panic("unimplemented declaration AST node: {}", (int)decl.ast_decl->kind);
    }
}

HirDecl* Checker::checkFuncDecl(size_t decl_number, AstNode* node) {
    auto* afunc_type = node->an_Func.func_type;
    auto* func_type = checkDeclTypeLabel(decl_number, node->an_Func.func_type);
    node->an_Func.symbol->type = func_type;

    std::vector<Symbol*> hparams;
    for (auto& aparam : afunc_type->an_TypeFunc.params) {
        auto* symbol = arena.New<Symbol>(
            mod.id,
            aparam.name,
            aparam.span,
            SYM_VAR,
            0,
            checkDeclTypeLabel(decl_number, aparam.type)
        );

        hparams.push_back(symbol);
    }

    auto* hfunc = allocDecl(HIR_FUNC, node->span);
    hfunc->ir_Func.symbol = node->an_Func.symbol;
    hfunc->ir_Func.params = arena.MoveVec(std::move(hparams));
    hfunc->ir_Func.return_type = func_type->ty_Func.return_type;
    hfunc->ir_Func.body = nullptr;

    return hfunc;
}

HirDecl* Checker::checkGlobalVarDecl(size_t decl_number, AstNode* node) {
    // TODO
}

/* -------------------------------------------------------------------------- */

Type* Checker::checkDeclTypeLabel(size_t decl_number, AstNode* type_label) {
    if (resolveTypeLabel(decl_number, type_label)) {
        // TODO: cycle error
    }

    // TODO: create type instance
}

/* -------------------------------------------------------------------------- */

bool Checker::resolveTypeLabel(size_t decl_number, AstNode* type_label) {
    switch (type_label->kind) {
    case AST_TYPE_PRIM:
        return false;
    case AST_DEREF:
    case AST_TYPE_ARRAY:
    case AST_TYPE_SLICE:
    case AST_SELECTOR:
    case AST_TYPE_FUNC:
    case AST_TYPE_ENUM:
    case AST_TYPE_STRUCT:
    case AST_IDENT:
    default:
        Panic("resolution not implemented for type AST");
    }
}
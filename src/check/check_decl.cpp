#include "checker.hpp"

static Symbol* getDeclSymbol(AstNode* node) {
    switch (node->kind) {
    case AST_FUNC:
        return node->an_Func.symbol;
    case AST_TYPEDEF:
        return node->an_TypeDef.symbol;
    case AST_VAR:
    case AST_CONST:
        return node->an_Var.symbol;
    }
}

void Checker::checkDecl(Decl* decl) {
    src_file = &mod.files[decl->file_number];

    switch (decl->color) {
    case COLOR_BLACK:
        return;
    case COLOR_GREY: {
        auto* start_symbol = getDeclSymbol(decl->ast_decl);
        std::string fmt_cycle(start_symbol->name);
        bool cycle_involves_const = false;
        
        for (size_t i = decl_number_stack.size() - 1; i >= 0; i--) {
            auto n = decl_number_stack[i];

            fmt_cycle += " -> ";

            auto* symbol = getDeclSymbol(mod.unsorted_decls[n]->ast_decl);
            fmt_cycle += symbol->name;
            if (symbol->flags & SYM_CONST) {
                cycle_involves_const = true;
            }

            if (n == curr_decl_number) {
                break;
            }
        }
        
        if (start_symbol->flags & SYM_TYPE) {
            if (cycle_involves_const) {
                fatal(start_symbol->span, "type depends cyclically on constant: {}", fmt_cycle);
            } else {
                fatal(start_symbol->span, "infinite type detected: {}", fmt_cycle);
            }
        } else {
            fatal(start_symbol->span, "initialization cycle detected: {}", fmt_cycle);
        }
    } break;
    case COLOR_WHITE:
        decl->color = COLOR_GREY;
        break;
    }

    switch (decl->ast_decl->kind) {
    case AST_FUNC:
        decl->hir_decl = checkFuncDecl(decl->ast_decl);
        break;
    case AST_VAR:
        decl->hir_decl = checkGlobalVar(decl->ast_decl);
        break;
    case AST_CONST:
        decl->hir_decl = checkGlobalConst(decl->ast_decl);
        mod.sorted_decls.push_back(decl);
        break;
    case AST_TYPEDEF:
        decl->hir_decl = checkTypeDef(decl->ast_decl);
        mod.sorted_decls.push_back(decl);
        break;
    }

    decl->color = COLOR_BLACK;
}

/* -------------------------------------------------------------------------- */

HirDecl* Checker::checkFuncDecl(AstNode* node) {
    auto* symbol = node->an_Func.symbol;
    auto& afunc_type = node->an_Func.func_type->an_TypeFunc;

    std::vector<Type*> param_types;
    std::vector<Symbol*> params;
    for (auto& aparam : afunc_type.params) {
        auto* param_type = checkTypeLabel(aparam.type, false);
        auto* param = arena.New<Symbol>(
            mod.id,
            aparam.name,
            aparam.span,
            SYM_VAR,
            0,
            param_type
        );

        param_types.push_back(param_type);
        params.push_back(param);
    }

    auto* return_type = checkTypeLabel(afunc_type.return_type, false);

    auto* func_type = allocType(TYPE_FUNC);
    func_type->ty_Func.param_types = arena.MoveVec(std::move(param_types));
    func_type->ty_Func.return_type = return_type;
    symbol->type = func_type;

    auto* hfunc = allocDecl(HIR_FUNC, node->span);
    hfunc->ir_Func.symbol = symbol;
    hfunc->ir_Func.params = arena.MoveVec(std::move(params));
    hfunc->ir_Func.return_type = return_type;
    hfunc->ir_Func.body = nullptr;

    return hfunc;
}

HirDecl* Checker::checkGlobalVar(AstNode* node) {
    auto* type = checkTypeLabel(node->an_Var.type, false);
    auto* symbol = node->an_Var.symbol;
    symbol->type = type;

    auto* hvar = allocDecl(HIR_GLOBAL_VAR, node->span);
    hvar->ir_GlobalVar.symbol = symbol;
    
    return hvar;
}

HirDecl* Checker::checkGlobalConst(AstNode* node) {
    auto* type = checkTypeLabel(node->an_Var.type, false);
    auto* symbol = node->an_Var.symbol;
    symbol->type = type;

    auto* value = checkComptime(node->an_Var.init);

    auto* hconst = allocDecl(HIR_GLOBAL_CONST, node->span);
    hconst->ir_GlobalConst.symbol = symbol;
    hconst->ir_GlobalConst.init = value;

    return hconst;
}

HirDecl* Checker::checkTypeDef(AstNode* node) {
    auto* base_type = checkTypeLabel(node->an_TypeDef.type, true);

    auto* symbol = node->an_TypeDef.symbol;
    symbol->type->ty_Named.type = base_type;

    HirDecl* hdecl;
    switch (base_type->kind) {
    case TYPE_STRUCT:
        hdecl = allocDecl(HIR_STRUCT, node->span);
        break;
    case TYPE_ENUM:
        hdecl = allocDecl(HIR_ENUM, node->span);
        break;
    default:
        hdecl = allocDecl(HIR_ALIAS, node->span);
        break;
    }

    hdecl->ir_TypeDef.symbol = symbol;
    return hdecl;
}

/* -------------------------------------------------------------------------- */

Type* Checker::checkTypeLabel(AstNode* node, bool should_expand) {
    switch (node->kind) {
    case AST_TYPE_PRIM:
        return node->an_TypePrim.prim_type;
    case AST_DEREF: {
        auto* elem_type = checkTypeLabel(node->an_Deref.expr, false);

        auto* ptr_type = allocType(TYPE_PTR);
        ptr_type->ty_Ptr.elem_type = elem_type;

        return ptr_type;
    } break;
    case AST_TYPE_FUNC:
        Panic("function type labels are not yet implemented");
        break;
    case AST_TYPE_ARRAY: {
        // We have to expand array types since they always contain more than one
        // element: they constitute a hard link between types.
        auto* elem_type = checkTypeLabel(node->an_TypeArray.elem_type, true);
        auto len = checkComptimeSize(node->an_TypeArray.len);
        if (len == 0) {
            error(node->an_TypeArray.len->span, "array cannot have zero length");
        }

        auto* arr_type = allocType(TYPE_ARRAY);
        arr_type->ty_Array.elem_type = elem_type;
        arr_type->ty_Array.len = len;
        return arr_type;
    } break;
    case AST_TYPE_SLICE: {
        auto* elem_type = checkTypeLabel(node->an_TypeSlice.elem_type, false);

        auto* slice_type = allocType(TYPE_SLICE);
        slice_type->ty_Slice.elem_type = elem_type;

        return slice_type;
    } break;
    case AST_TYPE_STRUCT: {
        std::vector<StructField> fields;
        std::unordered_map<std::string_view, size_t> name_map;

        size_t i = 0;
        for (auto& afield : node->an_TypeStruct.fields) {
            auto* field_type = checkTypeLabel(afield.type, true);

            fields.emplace_back(afield.name, field_type, afield.exported);
            name_map.emplace(afield.name, i++);
        }

        auto* struct_type = allocType(TYPE_STRUCT);
        struct_type->ty_Struct.fields = arena.MoveVec(std::move(fields));
        struct_type->ty_Struct.name_map = MapView(arena, std::move(name_map));
        struct_type->ty_Struct.llvm_type = nullptr;

        return struct_type;
    } break; 
    case AST_TYPE_ENUM: {
        std::unordered_map<std::string_view, uint64_t> tag_map;
        std::unordered_set<uint64_t> used_tags;
        uint64_t tag_counter = 0;

        for (auto* variant : node->an_TypeEnum.variants) {
            if (variant->kind == AST_IDENT) {
                tag_map.emplace(variant->an_Ident.name, tag_counter);
            } else {
                Assert(variant->kind == AST_NAMED_INIT, "bad ast in enum type");

                tag_counter = checkComptimeSize(node->an_NamedInit.init);
                tag_map.emplace(variant->an_NamedInit.name, tag_counter);
            }

            if (used_tags.contains(tag_counter)) {
                error(variant->span, "multiple enum variants with the same tag");
            } else {
                used_tags.insert(tag_counter);
            }

            tag_counter++;
        }

        auto* enum_type = allocType(TYPE_ENUM);
        enum_type->ty_Enum.tag_map = MapView(arena, std::move(tag_map));
        return enum_type;
    } break;
    case AST_IDENT: {
        auto [symbol, dep] = mustLookup(node->an_Ident.name, node->span);
        if (dep != nullptr) {
            fatal(node->span, "cannot use a module as a type");
        } 

        if (symbol->flags & SYM_TYPE) {
            auto* named_type = symbol->type;

            if (should_expand && named_type->ty_Named.type == nullptr) {
                decl_number_stack.push_back(curr_decl_number);
                curr_decl_number = symbol->decl_number;

                checkDecl(mod.unsorted_decls[curr_decl_number]);

                curr_decl_number = decl_number_stack.back();
                decl_number_stack.pop_back();

                src_file = &mod.files[mod.unsorted_decls[curr_decl_number]->file_number];
            }

            return named_type;
        } else {
            fatal(node->span, "cannot use a value as a type");
        }
    } break;
    case AST_SELECTOR: {
        auto* ident = node->an_Sel.expr;
        auto [_, dep] = mustLookup(ident->an_Ident.name, ident->span);
        if (dep == nullptr) {
            fatal(node->span, "{} must refer to an imported module", ident->an_Ident.name);
        }

        auto* symbol = findSymbolInDep(*dep, node->an_Sel.field_name);
        if (symbol == nullptr) {
            fatal(node->span, "module {} has no exported symbol named {}", dep->mod->name, node->an_Sel.field_name);
        }

        if (symbol->flags & SYM_TYPE) {
            return symbol->type;
        } else {
            fatal(node->span, "cannot use a value as a type");
        }
    } break;
    default:
        Panic("unimplemented type ast checking for {}", (int)node->kind);
    }

    return nullptr;
}
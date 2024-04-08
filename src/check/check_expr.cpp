#include "checker.hpp"

HirExpr* Checker::checkExpr(AstNode* node, Type* infer_type = nullptr) {
    HirExpr* hexpr;

    switch (node->kind) {
    case AST_TEST_MATCH:
    case AST_CAST:
    case AST_BINOP:
    case AST_UNOP:
    case AST_ADDR:
    case AST_DEREF:
    case AST_CALL:
    case AST_INDEX:
    case AST_SLICE:
    case AST_SELECTOR:
        hexpr = checkSelector(node, infer_type);
        break;
    case AST_NEW: {
        if (comptime_depth > 0) {
            fatal(node->span, "expression cannot be evaluated at compile-time");
        } else {
            expr_is_comptime = false;
        }

        auto* elem_type = checkTypeLabel(node->an_New.type, true);

        hexpr = allocExpr(HIR_NEW, node->span);
        hexpr->type = allocType(TYPE_PTR);
        hexpr->type->ty_Ptr.elem_type = elem_type;
        // TODO: revise for heap allocation
        hexpr->ir_New.alloc_mode = enclosing_return_type ? HIRMEM_STACK : HIRMEM_GLOBAL;
    } break;
    case AST_NEW_ARRAY:
        // TODO: revise for automatic allocation
        Panic("checking for NEW_ARRAY is not implemented");
        break;
    case AST_NEW_STRUCT:
        hexpr = checkNewStruct(node, infer_type);
        break;
    case AST_ARRAY_LIT:
        hexpr = checkArrayLit(node, infer_type);
        break;
    case AST_STRUCT_LIT:
        hexpr = checkStructLit(node, infer_type);
        break;
    case AST_IDENT: 
        hexpr = checkIdent(node);
        break;
    case AST_NUM_LIT:
        hexpr = allocExpr(HIR_NUM_LIT, node->span);
        hexpr->type = infer_type ? infer_type : newUntyped(UK_NUM);
        hexpr->ir_Num.value = node->an_Num.value;
        break;
    case AST_FLOAT_LIT:
        hexpr = allocExpr(HIR_FLOAT_LIT, node->span);
        hexpr->type = infer_type ? infer_type : newUntyped(UK_FLOAT);
        hexpr->ir_Float.value = node->an_Float.value;
        break;
    case AST_BOOL_LIT:
        hexpr = allocExpr(HIR_BOOL_LIT, node->span);
        hexpr->type = &prim_bool_type;
        hexpr->ir_Bool.value = node->an_Bool.value;
        break;
    case AST_RUNE_LIT:
        hexpr = allocExpr(HIR_NUM_LIT, node->span);
        hexpr->type = &prim_i32_type;
        hexpr->ir_Num.value = (uint64_t)node->an_Rune.value;
        break;
    case AST_STRING_LIT:
        hexpr = allocExpr(HIR_STRING_LIT, node->span);
        hexpr->type = &prim_string_type;
        hexpr->ir_String.value = node->an_String.value;
        break;
    case AST_NULL:
        hexpr = allocExpr(HIR_NULL, node->span);
        if (infer_type) {
            hexpr->type = infer_type;
        } else {
            hexpr->type = newUntyped(UK_NULL);
            null_spans.emplace_back(hexpr->type, hexpr->span);
        }
        break;
    default:
        Panic("expr checking is not implemented for {}", (int)node->kind);
        return nullptr;
    }

    return hexpr;
}

HirExpr* Checker::checkSelector(AstNode* node, Type* infer_type) {
    HirExpr* root_expr;

    auto* aroot = node->an_Sel.expr;
    switch (aroot->kind) {
    case AST_IDENT: { // name1.name2
        auto [symbol, dep] = mustLookup(aroot->an_Ident.name, aroot->span);
        if (dep != nullptr) { // module.name
            auto* imported_symbol = mustFindSymbolInDep(*dep, node->an_Sel.field_name, node->span);
            return checkStaticGet(imported_symbol, dep->mod->name, node->span);
        }

        if (symbol->flags & SYM_TYPE) { // Type.name2
            if (comptime_depth > 0) {
                if (symbol->type == nullptr) {
                    // Recursively expand comptime values.
                    Assert(symbol->parent_id == mod.id, "comptime is undetermined after module checking is completed");
                    checkDecl(mod.unsorted_decls[symbol->decl_number]);
                }
            }

            return checkEnumLit(node, symbol->type);
        }

        // Default to field access.
        root_expr = checkValueSymbol(symbol, aroot->span);
    } break;
    case AST_SELECTOR: { // [expr].name1.name2
        auto* asubroot = aroot->an_Sel.expr;

        if (asubroot->kind == AST_IDENT) { // name1.name2.name3
            auto [symbol, dep] = mustLookup(asubroot->an_Ident.name, asubroot->span);
            if (dep != nullptr) { // module.name1.name2
                auto* imported_symbol = mustFindSymbolInDep(*dep, aroot->an_Sel.field_name, aroot->span);

                if (imported_symbol->flags & SYM_TYPE) { // module.Type.name
                    return checkEnumLit(node, imported_symbol->type);
                }

                root_expr = checkStaticGet(imported_symbol, dep->mod->name, aroot->span);
            } else if (symbol->flags & SYM_TYPE) { // Type.name1.name2
                if (comptime_depth > 0) {
                    if (symbol->type == nullptr) {
                        // Recursively expand comptime values.
                        Assert(symbol->parent_id == mod.id, "comptime is undetermined after module checking is completed");
                        checkDecl(mod.unsorted_decls[symbol->decl_number]);
                    }
                }

                root_expr = checkEnumLit(aroot, symbol->type);
            } else { // value.name1.name2
                root_expr = checkValueSymbol(symbol, asubroot->span);
                root_expr = checkField(root_expr, aroot->an_Sel.field_name, aroot->span);
            }
        } else { 
            // Default to field access.
            root_expr = checkExpr(aroot);
        }
    } break;
    case AST_DOT: // .name
        if (infer_type) {
            return checkEnumLit(node, infer_type);
        } else {
            fatal(node->span, "cannot infer type of enum literal");
        }
        break;
    default:
        root_expr = checkExpr(node->an_Sel.expr);
        break;
    }

    return checkField(root_expr, node->an_Sel.field_name, node->span);
}

HirExpr* Checker::checkField(HirExpr* root, std::string_view field_name, const TextSpan& span) {
    Type* display_type = root->type;
    auto* root_type = display_type->Inner();

    bool is_auto_deref = false;
    if (root_type->kind == TYPE_PTR) {
        is_auto_deref = true;
        display_type = root_type->ty_Ptr.elem_type;
        root_type = display_type->FullUnwrap();
    } else {
        root_type = root_type->FullUnwrap();
    }

    Type* result_type = nullptr;
    size_t field_index = 0;
    switch (root_type->kind) {
    case TYPE_ARRAY:
    case TYPE_SLICE:
        if (field_name == "_ptr") {
            result_type = allocType(TYPE_PTR);

            // Should be ok for both ty_Array and ty_Slice.
            result_type->ty_Ptr.elem_type = root_type->ty_Slice.elem_type;
            field_index = 0;
        } else if (field_name == "_len") {
            result_type = platform_int_type;
            field_index = 1;
        }
        break;
    case TYPE_STRUCT: {
        auto maybe_field_index = root_type->ty_Struct.name_map.try_get(field_name);
        if (maybe_field_index) {
            field_index = maybe_field_index.value();

            auto& field = root_type->ty_Struct.fields[field_index];
            if (field.exported || display_type->ty_Named.mod_id == mod.id) {
                result_type = field.type;
            } else {
                fatal(span, "field {} of {} is not exported", field_name, display_type->ToString());
            }
        }
    } break;
    }

    if (result_type == nullptr) {
        fatal(span, "type {} has no field named {}", display_type->ToString());
    }

    auto* hexpr = allocExpr(is_auto_deref ? HIR_DEREF_FIELD : HIR_FIELD, span);
    hexpr->type = result_type;
    hexpr->ir_Field.expr = root;
    hexpr->ir_Field.field_index = field_index;
    return hexpr;
}

HirExpr* Checker::checkEnumLit(AstNode* node, Type* type) {
    auto* enum_type = type->FullUnwrap();
    if (enum_type->kind != TYPE_ENUM) {
        fatal(node->span, "{} is not an enum type", type->ToString());
    }

    auto maybe_tag_value = enum_type->ty_Enum.tag_map.try_get(node->an_Sel.field_name);
    if (!maybe_tag_value) {
        fatal(node->span, "enum {} has no variant named {}", type->ToString(), node->an_Sel.field_name);
    }

    auto* hexpr = allocExpr(HIR_ENUM_LIT, node->span);
    hexpr->type = type;
    hexpr->ir_EnumLit.tag_value = maybe_tag_value.value();
    return hexpr;
}

HirExpr* Checker::checkStaticGet(Symbol* imported_symbol, std::string_view mod_name, const TextSpan& span) {
    if (imported_symbol->flags & SYM_TYPE) {
        fatal(span, "cannot use a type as a value");
    }

    if ((imported_symbol->flags & SYM_COMPTIME) == 0) {
        if (comptime_depth > 0) {
            fatal(span, "value of {}.{} cannot be determined at compile time", mod_name, imported_symbol->name);
        } else {
            expr_is_comptime = false;
        }
    } 

    auto* hexpr = allocExpr(HIR_STATIC_GET, span);
    hexpr->type = imported_symbol->type;
    hexpr->assignable = !imported_symbol->immut;
    hexpr->ir_Ident.symbol = imported_symbol;
    return hexpr;
}

HirExpr* Checker::checkNewStruct(AstNode* node, Type* infer_type) {
    auto* atype = node->an_StructLit.type;

    Type* elem_type;
    if (atype->kind == AST_DOT) {
        if (infer_type) {
            infer_type = infer_type->Inner();
            if (infer_type->kind == TYPE_PTR) {
                elem_type = infer_type->ty_Ptr.elem_type;
            } else {
                fatal(atype->span, "cannot infer type of struct literal");
            }
        } else {
            fatal(atype->span, "cannot infer type of struct literal");
        }
    } else {
        elem_type = checkTypeLabel(atype, true);
    }

    auto* struct_type = elem_type->FullUnwrap();
    if (struct_type->kind != TYPE_STRUCT) {
        fatal(atype->span, "{} is not a struct type", elem_type->ToString());
    }

    auto field_inits = checkFieldInits(node->an_StructLit.field_inits, struct_type, elem_type);

    Type* ptr_type;
    if (infer_type) {
        ptr_type = infer_type;
    } else {
        ptr_type = allocType(TYPE_PTR);
        ptr_type->ty_Ptr.elem_type = elem_type;
    }

    auto* hexpr = allocExpr(HIR_NEW_STRUCT, node->span);
    hexpr->type = ptr_type;
    hexpr->ir_NewStruct.field_inits = arena.MoveVec(std::move(field_inits));
    // TODO: heap allocation
    hexpr->ir_NewStruct.alloc_mode = enclosing_return_type ? HIRMEM_STACK : HIRMEM_GLOBAL;
    return hexpr;
}

HirExpr* Checker::checkArrayLit(AstNode* node, Type* infer_type) {
    Type* elem_infer_type = nullptr;
    if (infer_type) {
        infer_type = infer_type->Inner();

        if (infer_type->kind == TYPE_ARRAY)
            elem_infer_type = infer_type->ty_Array.elem_type;
        else if (infer_type->kind == TYPE_SLICE)
            elem_infer_type = infer_type->ty_Slice.elem_type;
    }

    auto& aitems = node->an_ExprList.exprs;
    std::vector<HirExpr*> items;
    for (auto* aitem : aitems) {
        items.push_back(checkExpr(aitem, elem_infer_type));
    }

    auto* first_type = items[0]->type;
    for (size_t i = 1; i < items.size(); i++) {
        mustEqual(items[i]->span, first_type, items[i]->type);
    }

    auto* arr_type = allocType(TYPE_ARRAY);
    arr_type->ty_Array.elem_type = first_type;
    arr_type->ty_Array.len = (uint64_t)items.size();

    auto* hexpr = allocExpr(HIR_ARRAY_LIT, node->span);
    hexpr->type = arr_type;
    hexpr->ir_ArrayLit.items = arena.MoveVec(std::move(items));
    // TODO: heap allocation
    hexpr->ir_ArrayLit.alloc_mode = enclosing_return_type ? HIRMEM_STACK : HIRMEM_GLOBAL;
    return hexpr;
}

HirExpr* Checker::checkStructLit(AstNode* node, Type* infer_type) {
    auto* atype = node->an_StructLit.type;

    Type* type;
    if (atype->kind == AST_DOT) {
        if (infer_type) {
            type = infer_type;
        } else {
            fatal(atype->span, "cannot infer type of struct literal");
        }
    } else {
        type = checkTypeLabel(atype, true);
    }

    auto* struct_type = type->FullUnwrap();
    if (struct_type->kind != TYPE_STRUCT) {
        fatal(atype->span, "{} is not a struct type", type->ToString());
    }

    auto field_inits = checkFieldInits(node->an_StructLit.field_inits, struct_type, type);

    auto* hexpr = allocExpr(HIR_STRUCT_LIT, node->span);
    hexpr->type = type;
    hexpr->ir_StructLit.field_inits = arena.MoveVec(std::move(field_inits));
    return hexpr;
}

std::vector<HirFieldInit> Checker::checkFieldInits(std::span<AstNode*> afield_inits, Type* struct_type, Type* display_type) {
    std::vector<HirFieldInit> field_inits;

    size_t field_index = 0;
    for (auto* ainit : afield_inits) {
        if (ainit->kind == AST_NAMED_INIT) {
            auto maybe_field_index = struct_type->ty_Struct.name_map.try_get(ainit->an_NamedInit.name);
            if (!maybe_field_index) {
                error(ainit->span, "struct {} has no field named {}", display_type->ToString(), ainit->an_NamedInit.name);
                continue;
            }

            field_index = maybe_field_index.value();
            ainit = ainit->an_NamedInit.init;
        } else {
            if (field_index > struct_type->ty_Struct.fields.size()) {
                error(
                    ainit->span, 
                    "struct has {} fields but {} values are specified", 
                    struct_type->ty_Struct.fields.size(), 
                    afield_inits.size()
                );
                break;
            }
        }
        
        auto* field_type = struct_type->ty_Struct.fields[field_index].type;
        auto* hinit = checkExpr(ainit, field_type);

        if (mustSubType(ainit->span, hinit->type, field_type)) {
            hinit = createImplicitCast(hinit, field_type);
        }

        field_inits.emplace_back(hinit, field_index);
        field_index++;
    }

    return field_inits;
}

HirExpr* Checker::checkIdent(AstNode* node) {
    auto [symbol, dep] = mustLookup(node->an_Ident.name, node->span);
    if (dep != nullptr) {
        fatal(node->span, "cannot use a module as a value");
    }

    if (symbol->flags & SYM_TYPE) {
        fatal(node->span, "cannot use a type as a value");
    }

    return checkValueSymbol(symbol, node->span);
}

/* -------------------------------------------------------------------------- */

HirExpr* Checker::checkValueSymbol(Symbol* symbol, const TextSpan& span) {
    if (symbol->flags & SYM_COMPTIME) {
        if (comptime_depth > 0) {
            if (symbol->type == nullptr) {
                // Recursively expand comptime values.
                Assert(symbol->parent_id == mod.id, "comptime is undetermined after module checking is completed");
                checkDecl(mod.unsorted_decls[symbol->decl_number]);
            }
        }
    } else if (comptime_depth > 0) {
        fatal(span, "value of {} cannot be determined at compile time", symbol->name);
    } else {
        expr_is_comptime = false;
    }

    auto* hexpr = allocExpr(HIR_IDENT, span);
    hexpr->type = symbol->type;
    hexpr->assignable = !symbol->immut;
    hexpr->ir_Ident.symbol = symbol;
    return hexpr;
}
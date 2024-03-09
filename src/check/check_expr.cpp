#include "checker.hpp"

void Checker::checkExpr(AstExpr* node, Type* infer_type) {
    switch (node->kind) {
    case AST_CAST:
        checkExpr(node->an_Cast.src, node->type);
        mustCast(node->an_Cast.src->span, node->an_Cast.src->type, node->type);
        break;
    case AST_BINOP:
        checkExpr(node->an_Binop.lhs);
        checkExpr(node->an_Binop.rhs);

        node->type = mustApplyBinaryOp(
            node->span, 
            node->an_Binop.op, 
            node->an_Binop.lhs->type, 
            node->an_Binop.rhs->type
        );
        break;
    case AST_UNOP:
        checkExpr(node->an_Unop.operand);

        node->type = mustApplyUnaryOp(node->span, node->an_Unop.op, node->an_Unop.operand->type);
        break;
    case AST_ADDR:
        checkExpr(node->an_Addr.elem);

        if (!node->an_Addr.elem->IsLValue()) {
            error(node->span, "cannot take address of unaddressable value");
        }

        node->type = AllocType(arena, TYPE_PTR);
        node->type->ty_Ptr.elem_type = node->an_Addr.elem->type;
        break;
    case AST_DEREF:
        checkDeref(node);
        break;
    case AST_CALL:
        checkCall(node);
        break;
    case AST_INDEX:
        checkIndex(node);
        break;
    case AST_SLICE:
        checkSlice(node);
        break;
    case AST_FIELD:
        checkField(node, false);
        break;
    case AST_ARRAY:
        checkArray(node, infer_type);
        break;
    case AST_NEW: 
        checkNewExpr(node);
        break;
    case AST_IDENT: {
        auto* dep = checkIdentOrGetImport(node, false);
        if (dep != nullptr) {
            fatal(node->span, "imported module cannot be used as a value");
        }
    } break;   
    case AST_INT:
        if (node->type == nullptr) {
            node->type = newUntyped(UK_NUM);
        }
        break;
    case AST_FLOAT:
        node->type = newUntyped(UK_FLOAT);
        break;
    case AST_STR:
    case AST_BOOL:
        // Nothing to do :)
        break;
    case AST_NULL:
        if (infer_type) {
            node->type = infer_type;
        } else {
            fatal(node->span, "unable to infer type of null");
        }
        break;
    case AST_STRUCT_LIT_POS:
    case AST_STRUCT_LIT_NAMED:
    case AST_STRUCT_PTR_LIT_POS:
    case AST_STRUCT_PTR_LIT_NAMED:
        checkStructLit(node, infer_type);
        break;
    case AST_STRUCT_LIT_TYPE:
        Panic("ast struct lit type outside of ast struct lit");
        break;
    default:
        Panic("checking not implemented for expr {}", (int)node->kind);
    }
}

/* -------------------------------------------------------------------------- */

void Checker::checkDeref(AstExpr* node) {
    auto* ptr = node->an_Deref.ptr;
    checkExpr(ptr);

    auto* ptr_type = ptr->type->Inner();
    if (ptr_type->Inner()->kind == TYPE_PTR) {
        node->type = ptr_type->ty_Ptr.elem_type;

        // TODO: pointer constancy
        node->immut = ptr->immut;
    } else {
        fatal(ptr->span, "expected a pointer type but got {}", ptr->type->ToString());
    }
}

void Checker::checkCall(AstExpr* node) {
    is_comptime_expr = false;

    auto& call = node->an_Call;

    checkExpr(call.func);

    auto* func_type = call.func->type->Inner();
    if (func_type->kind != TYPE_FUNC) {
        fatal(call.func->span, "expected a function type but got {}", call.func->type->ToString());
    }

    if (call.args.size() != func_type->ty_Func.param_types.size()) {
        fatal(node->span, "function expects {} arguments but got {}", func_type->ty_Func.param_types.size(), call.args.size());
    }

    for (int i = 0; i < call.args.size(); i++) {
        checkExpr(call.args[i], func_type->ty_Func.param_types[i]);
        mustSubType(call.args[i]->span, call.args[i]->type, func_type->ty_Func.param_types[i]);
    }

    node->type = func_type->ty_Func.return_type;
}

void Checker::checkIndex(AstExpr* node) {
    auto& index = node->an_Index;
    checkExpr(index.array);

    // TODO: platform int type
    checkExpr(index.index, &prim_i64_type);

    auto* array_type = index.array->type->Inner();
    if (array_type->kind == TYPE_ARRAY || array_type->kind == TYPE_STRING) {
        mustIntType(index.index->span, index.index->type);

        node->type = array_type->ty_Array.elem_type;

        node->immut = index.array->immut || array_type->kind == TYPE_STRING;
    } else {
        fatal(index.array->span, "{} is not a string or array type", index.array->type->ToString());
    }
}

void Checker::checkSlice(AstExpr* node) {
    auto& slice = node->an_Slice;

    checkExpr(slice.array);

    if (slice.start_index)
        // TODO: platform int type
        checkExpr(slice.start_index, &prim_i64_type);

    if (slice.end_index)
        // TODO: platform int type
        checkExpr(slice.end_index, &prim_i64_type);

    auto* inner_type = slice.array->type->Inner();
    if (inner_type->kind == TYPE_ARRAY || inner_type->kind == TYPE_STRING) {
        if (slice.start_index)
            mustIntType(slice.start_index->span, slice.start_index->type);

        if (slice.end_index)
            mustIntType(slice.end_index->span, slice.end_index->type);

        node->type = inner_type;

        node->immut = slice.array->immut;
    } else {
        fatal(slice.array->span, "{} is not a string or array type", slice.array->type->ToString());
    }
}


void Checker::checkField(AstExpr* node, bool expect_type) {
    auto& fld = node->an_Field;
    
    if (fld.root->kind == AST_IDENT) {
        auto* dep= checkIdentOrGetImport(fld.root, false);
        if (dep != nullptr) {
            auto* mod = dep->mod;

            auto sentry_it = mod->symbol_table.find(fld.field_name);
            if (sentry_it != mod->symbol_table.end()) {
                auto& sentry = sentry_it->second;
                if ((sentry.symbol->flags & SYM_EXPORTED) == 0) {
                    fatal(node->span, "symbol {} is not exported by module {}", fld.field_name, mod->name);
                }

                auto* imported_sym = sentry.symbol;
                node->type = imported_sym->type;
                node->immut = imported_sym->immut;

                if (!expect_type && (imported_sym->flags & SYM_TYPE)) {
                    fatal(node->span, "cannot use type as value");
                } else if (expect_type && (imported_sym->flags & SYM_TYPE) == 0) {
                    fatal(node->span, "expected type not value");
                }

                if ((imported_sym->flags & SYM_COMPTIME) == 0) {
                    is_comptime_expr = false;
                }
                
                fld.imported_sym = imported_sym;
                node->kind = AST_STATIC_GET;
                dep->usages.insert(sentry.def_number);
                return;
            } else {
                fatal(node->span, "module {} has no symbol named {}", mod->name, fld.field_name);
            }
        }
    } else {
        checkExpr(fld.root);
    }

    auto root_type = fld.root->type->Inner();
    if (root_type->kind == TYPE_PTR) {
        root_type = root_type->ty_Ptr.elem_type;
    }

    switch (root_type->kind) {
    case TYPE_NAMED: {
        auto base_type = root_type->ty_Named.type;

        if (base_type->kind == TYPE_STRUCT) {
            size_t i = 0;
            for (auto& field : base_type->ty_Struct.fields) {
                if (field.name == fld.field_name) {
                    if (root_type->ty_Named.mod_id != mod.id && !field.exported) {
                        fatal(node->span, "struct field {} is not exported", field.name);
                    }

                    node->type = field.type;
                    fld.field_index = i;
                    return;
                }

                i++;
            }
        }
    } break;
    case TYPE_STRUCT: { // Anonymous struct
        size_t i = 0;
        for (auto& field : root_type->ty_Struct.fields) {
            if (field.name == fld.field_name) {
                node->type = field.type;
                fld.field_index = i;
                return;
            }

            i++;
        }
    } break;
    case TYPE_ARRAY:
    case TYPE_STRING:
        if (fld.field_name == "_ptr") {
            node->type = AllocType(arena, TYPE_PTR);
            node->type->ty_Ptr.elem_type = root_type->ty_Array.elem_type;
            return;
        } else if (fld.field_name == "_len") {
            // TODO: platform sizes?
            node->type = &prim_i64_type;
            return;
        }
        break;
    }

    fatal(node->span, "{} has no field named {}", fld.root->type->ToString(), fld.field_name);
}

Module::Dependency* Checker::checkIdentOrGetImport(AstExpr* node, bool expect_type) {
    auto name = node->an_Ident.temp_name;
    Symbol* sym { nullptr };
    for (int i = scope_stack.size() - 1; i >= 0; i--) {
        auto& scope = scope_stack[i];

        auto it = scope.find(name);
        if (it != scope.end()) {
            sym = it->second;
        }
    }

    if (sym == nullptr) {
        auto import_it = src_file->import_table.find(name);
        if (import_it != src_file->import_table.end()) {
            node->an_Ident.dep_id = import_it->second;
            return &mod.deps[import_it->second];
        }

        auto it = mod.symbol_table.find(name);
        if (it != mod.symbol_table.end()) {
            sym = it->second.symbol;

            init_graph.back().edges.insert(it->second.def_number);
        } else if (
            core_dep != nullptr && 
            (it = core_dep->mod->symbol_table.find(name)) != core_dep->mod->symbol_table.end()
        ) {
            sym = it->second.symbol;
            core_dep->usages.insert(it->second.def_number);
        } else {
            fatal(node->span, "undefined symbol: {}", name);
        }
    }
    
    if (!expect_type && (sym->flags & SYM_TYPE)) {
        fatal(node->span, "cannot use type as value");
    } else if (expect_type && (sym->flags & SYM_TYPE) == 0) {
        fatal(node->span, "expected type not value");
    }

    if ((sym->flags & SYM_COMPTIME) == 0) {
        is_comptime_expr = false;
    }

    node->an_Ident.symbol = sym;
    node->type = sym->type;
    node->immut = sym->immut;
    return nullptr;
}

/* -------------------------------------------------------------------------- */

void Checker::checkArray(AstExpr* node, Type* infer_type) {
    auto& arr = node->an_Array;
    Assert(arr.elems.size() > 0, "empty array literals are not implemented yet");

    Type* elem_infer_type = nullptr;
    if (infer_type) {
        infer_type = infer_type->Inner();

        if (infer_type->kind == TYPE_ARRAY)
            elem_infer_type = infer_type->ty_Array.elem_type;
    }

    for (auto& elem : arr.elems) {
        checkExpr(elem, elem_infer_type);
    }

    auto* first_type = arr.elems[0]->type;
    for (int i = 1; i < arr.elems.size(); i++) {
        mustEqual(arr.elems[i]->span, first_type, arr.elems[i]->type);
    }

    node->type = AllocType(arena, TYPE_ARRAY);
    node->type->ty_Array.elem_type = first_type;

    // Move array literal to global memory if necessary.
    if (enclosing_return_type == nullptr) {
        arr.alloc_mode = A_ALLOC_GLOBAL;
    } else {
        arr.alloc_mode = A_ALLOC_STACK;
    }
}

void Checker::checkNewExpr(AstExpr* node) {
    node->an_New.alloc_mode = A_ALLOC_STACK;

    auto* size_expr = node->an_New.size_expr;
    if (size_expr) {
        // TODO: platform int type
        bool outer_is_comptime = is_comptime_expr;
        is_comptime_expr = true;
        checkExpr(size_expr, &prim_i64_type);

        if (!is_comptime_expr) {
            node->an_New.alloc_mode = A_ALLOC_HEAP;
            outer_is_comptime = false;
        }

        is_comptime_expr = outer_is_comptime;

        mustIntType(size_expr->span, size_expr->type);

        node->type = AllocType(arena, TYPE_ARRAY);
        node->type->ty_Array.elem_type = node->an_New.elem_type;
    } else {
        node->type = AllocType(arena, TYPE_PTR);
        node->type->ty_Ptr.elem_type = node->an_New.elem_type;
    }

    
    if (enclosing_return_type == nullptr && node->an_New.alloc_mode == A_ALLOC_STACK) {
        node->an_New.alloc_mode = A_ALLOC_GLOBAL;
    }
}

void Checker::checkStructLit(AstExpr* node, Type* infer_type) {
    // Should be same location for Pos and Named :)
    auto* root_node = node->an_StructLitPos.root;

    switch (root_node->kind) {
    case AST_IDENT: {
        auto* dep = checkIdentOrGetImport(root_node, true);
        if (dep != nullptr) {
            fatal(node->span, "imported module cannot be used as a value");
        }
    } break;
    case AST_FIELD:
        checkField(root_node, true);
        break;
    case AST_STRUCT_LIT_TYPE: 
        if (root_node->type == nullptr) {
            if (infer_type) {
                if (node->kind == AST_STRUCT_PTR_LIT_NAMED || node->kind == AST_STRUCT_PTR_LIT_POS) {
                    if (infer_type->kind == TYPE_PTR) {
                        root_node->type = infer_type->ty_Ptr.elem_type;
                    } else {
                        fatal(node->span, "cannot infer type of struct literal");
                    }
                } else {
                    root_node->type = infer_type;
                }
            } else {
                fatal(node->span, "cannot infer type of struct literal");
            }
        }
        break;
    default:
        fatal(root_node->span, "expected a struct type label or '.'");
    }

    auto* struct_type = root_node->type->Inner();
    if (struct_type->kind == TYPE_NAMED) {
        struct_type = struct_type->ty_Named.type;
    }
    
    if (struct_type->kind != TYPE_STRUCT) {
        fatal(root_node->span, "{} is not a struct type", root_node->type->ToString());
    }

    if (node->kind == AST_STRUCT_LIT_POS || node->kind == AST_STRUCT_PTR_LIT_POS) {
        auto& field_inits = node->an_StructLitPos.field_inits;
        for (size_t i = 0; i < field_inits.size(); i++) {
            if (i >= struct_type->ty_Struct.fields.size()) {
                error(
                    root_node->span, 
                    "{} has only {} fields but initializer provides {} values", 
                    root_node->type->ToString(), 
                    i, field_inits.size()
                );
                break;
            }

            checkExpr(field_inits[i], struct_type->ty_Struct.fields[i].type);
            mustSubType(field_inits[i]->span, field_inits[i]->type, struct_type->ty_Struct.fields[i].type);
        }

        if (enclosing_return_type == nullptr) {
            node->an_StructLitPos.alloc_mode = A_ALLOC_GLOBAL;
        } else {
            node->an_StructLitPos.alloc_mode = A_ALLOC_STACK;
        }
    } else { // Named
        // O(n^2) kekw
        bool found_match = false;
        for (auto& field_init : node->an_StructLitNamed.field_inits) {
            found_match = false;

            size_t i = 0;
            for (auto& field : struct_type->ty_Struct.fields) {
                if (field.name == field_init.ident->an_Ident.temp_name) {
                    checkExpr(field_init.expr, field.type);
                    mustSubType(field_init.expr->span, field_init.expr->type, field.type);
                    field_init.field_index = i;
                    found_match = true;
                }

                i++;
            }

            if (!found_match) {
                error(
                    field_init.ident->span, 
                    "struct {} has no field named {}", 
                    root_node->type->ToString(), 
                    field_init.ident->an_Ident.temp_name
                );
            }
        }

        if (enclosing_return_type == nullptr) {
            node->an_StructLitNamed.alloc_mode = A_ALLOC_GLOBAL;
        } else {
            node->an_StructLitNamed.alloc_mode = A_ALLOC_STACK;
        }
    }

    if (node->kind == AST_STRUCT_LIT_NAMED || node->kind == AST_STRUCT_LIT_POS) {
        node->type = root_node->type;
    } else {
        node->type = AllocType(arena, TYPE_PTR);
        node->type->ty_Ptr.elem_type = root_node->type;
    }
}

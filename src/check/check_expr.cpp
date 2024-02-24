#include "checker.hpp"

void Checker::checkExpr(AstExpr* node) {
    switch (node->kind) {
    case AST_CAST:
        checkExpr(node->an_Cast.src);
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
        checkField(node);
        break;
    case AST_ARRAY:
        checkArray(node);
        break;
    case AST_NEW: 
        checkNewExpr(node);
        break;
    case AST_IDENT: {
        auto* entry = checkIdentOrGetImport(node);
        if (entry != nullptr) {
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
        Panic("null outside of function call");
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
        if (call.args[i]->kind == AST_NULL) {
            call.args[i]->type = func_type->ty_Func.param_types[i];
        } else {
            checkExpr(call.args[i]);
            mustSubType(call.args[i]->span, call.args[i]->type, func_type->ty_Func.param_types[i]);
        }
    }

    node->type = func_type->ty_Func.return_type;
}

void Checker::checkIndex(AstExpr* node) {
    auto& index = node->an_Index;
    checkExpr(index.array);
    checkExpr(index.index);

    auto* array_type = index.array->type->Inner();
    if (array_type->kind == TYPE_ARRAY) {
        mustIntType(index.index->span, index.index->type);

        node->type = array_type->ty_Array.elem_type;

        // TODO: array constancy
        node->immut = index.array->immut;
    } else {
        fatal(index.array->span, "{} is not array type", index.array->type->ToString());
    }
}

void Checker::checkSlice(AstExpr* node) {
    auto& slice = node->an_Slice;

    checkExpr(slice.array);

    if (slice.start_index)
        checkExpr(slice.start_index);

    if (slice.end_index)
        checkExpr(slice.end_index);

    auto* inner_type = slice.array->type->Inner();
    if (inner_type->kind == TYPE_ARRAY) {
        if (slice.start_index)
            mustIntType(slice.start_index->span, slice.start_index->type);

        if (slice.end_index)
            mustIntType(slice.end_index->span, slice.end_index->type);

        node->type = inner_type;

        // TODO: array constancy
        node->immut = slice.array->immut;
    } else {
        fatal(slice.array->span, "{} is not array type", slice.array->type->ToString());
    }
}


void Checker::checkField(AstExpr* node) {
    auto& fld = node->an_Field;
    if (fld.root->kind == AST_IDENT) {
        auto* entry = checkIdentOrGetImport(fld.root);
        if (entry != nullptr) {
            auto* mod = src_file.parent->deps[entry->dep_id].mod;

            auto sym_it = mod->symbol_table.find(fld.field_name);
            if (sym_it != mod->symbol_table.end() && sym_it->second->exported) {
                fld.imported_sym = sym_it->second;
                node->type = fld.imported_sym->type;
                node->immut = fld.imported_sym->immut;
                
                entry->usages.insert(fld.imported_sym->name);
                return;
            } else {
                fatal(node->span, "module {} contains no exported symbol named {}", mod->name, fld.field_name);
            }
        }
    } else {
        checkExpr(fld.root);
    }

    auto root_type = fld.root->type->Inner();
    if (root_type->kind == TYPE_ARRAY) {
        if (fld.field_name == "_ptr") {
            node->type = AllocType(arena, TYPE_PTR);
            node->type->ty_Ptr.elem_type = root_type->ty_Array.elem_type;
        } else if (fld.field_name == "_len") {
            // TODO: platform sizes?
            node->type = &prim_i64_type;
        } else {
            fatal(node->span, "{} has no field named {}", fld.root->type->ToString(), fld.field_name);
        }
    } else {
        fatal(fld.root->span, "{} is not array type", fld.root->type->ToString());
    }
}

SourceFile::ImportEntry* Checker::checkIdentOrGetImport(AstExpr* node) {
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
        auto import_it = src_file.import_table.find(name);
        if (import_it != src_file.import_table.end()) {
            return &import_it->second;
        }

        auto it = src_file.parent->symbol_table.find(name);
        if (it != src_file.parent->symbol_table.end()) {
            sym = it->second;
        } else {
            fatal(node->span, "undefined symbol: {}", name);
        }
    }
    
    node->an_Ident.symbol = sym;
    node->type = sym->type;
    node->immut = sym->immut;
    return nullptr;
}

/* -------------------------------------------------------------------------- */

void Checker::checkArray(AstExpr* node) {
    auto& arr = node->an_Array;
    Assert(arr.elems.size() > 0, "empty array literals are not implemented yet");

    for (auto& elem : arr.elems) {
        checkExpr(elem);
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
    auto* size_expr = node->an_New.size_expr;
    if (size_expr) {
        checkExpr(size_expr);
        mustIntType(size_expr->span, size_expr->type);

        node->type = AllocType(arena, TYPE_ARRAY);
        node->type->ty_Array.elem_type = node->an_New.elem_type;
    } else {
        node->type = AllocType(arena, TYPE_PTR);
        node->type->ty_Ptr.elem_type = node->an_New.elem_type;
    }

    if (enclosing_return_type == nullptr) {
        // TODO: check for non-comptime sizes 
        node->an_New.alloc_mode = A_ALLOC_GLOBAL;
    } else {
        node->an_New.alloc_mode = A_ALLOC_STACK;
    }
}

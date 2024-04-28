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

    return nullptr;
}

static std::pair<std::string_view, bool> getDeclNameAndConst(AstNode* node) {
    switch (node->kind) {
    case AST_METHOD:
        return { node->an_Method.name, false };
    case AST_FACTORY:
        // TODO: get the factory name
        return { "factory", false };
    default: {
        auto* symbol = getDeclSymbol(node);
        return { symbol->name, symbol->flags & SYM_CONST };
    } break;
    }
}

void Checker::checkDecl(Decl* decl) {
    src_file = &mod.files[decl->file_number];

    switch (decl->color) {
    case COLOR_BLACK:
        return;
    case COLOR_GREY:
        reportCycle(decl);
        throw CompileError{};
    case COLOR_WHITE:
        decl->color = COLOR_GREY;
        break;
    }

    switch (decl->ast_decl->kind) {
    case AST_FUNC:
        decl->hir_decl = checkFuncDecl(decl->ast_decl);
        break;
    case AST_METHOD:
        decl->hir_decl = checkMethodDecl(decl->ast_decl);
        break;
    case AST_FACTORY:
        decl->hir_decl = checkFactoryDecl(decl->ast_decl);
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

bool Checker::addToInitOrder(Decl* decl) {
    switch (decl->color) {
    case COLOR_BLACK:
        break;
    case COLOR_WHITE: {
        decl->color = COLOR_GREY;

        auto& edges = init_graph[curr_decl_number];
        decl_number_stack.push_back(curr_decl_number);

        for (size_t edge_number : edges) {
            curr_decl_number = edge_number;
            if (addToInitOrder(mod.unsorted_decls[curr_decl_number])) {
                decl->color = COLOR_BLACK;
                return true;
            }
        }

        curr_decl_number = decl_number_stack.back();
        decl_number_stack.pop_back();

        mod.sorted_decls.push_back(decl);
        decl->color = COLOR_BLACK;
        return false;
    } break;
    case COLOR_GREY:
        if (decl->hir_decl->kind == HIR_GLOBAL_VAR) {
            reportCycle(decl);
            decl->color = COLOR_BLACK;
            return true;
        }
        break;
    }

    return false;
}

void Checker::reportCycle(Decl* decl) {
    auto* start_symbol = getDeclSymbol(decl->ast_decl);
    std::string fmt_cycle(start_symbol->name);
    bool cycle_involves_const = false;
    
    for (size_t i = decl_number_stack.size() - 1; i >= 0; i--) {
        auto n = decl_number_stack[i];

        fmt_cycle += " -> ";

        auto [name, is_const] = getDeclNameAndConst(mod.unsorted_decls[n]->ast_decl);
        fmt_cycle += name;
        if (is_const) {
            cycle_involves_const = true;
        }

        if (n == curr_decl_number) {
            break;
        }
    }
    
    if (start_symbol->flags & SYM_TYPE) {
        if (cycle_involves_const) {
            error(start_symbol->span, "type depends cyclically on constant: {}", fmt_cycle);
        } else {
            error(start_symbol->span, "infinite type detected: {}", fmt_cycle);
        }
    } else {
        error(start_symbol->span, "initialization cycle detected: {}", fmt_cycle);
    }
}

/* -------------------------------------------------------------------------- */

HirDecl* Checker::checkFuncDecl(AstNode* node) {
    auto* symbol = node->an_Func.symbol;

    std::vector<Symbol*> params;
    auto* func_type = checkFuncSignature(node->an_Func.func_type, params);
    symbol->type = func_type;

    auto* hfunc = allocDecl(HIR_FUNC, node->span);
    hfunc->ir_Func.symbol = symbol;
    hfunc->ir_Func.params = arena.MoveVec(std::move(params));
    hfunc->ir_Func.return_type = func_type->ty_Func.return_type;
    hfunc->ir_Func.body = nullptr;

    return hfunc;
}

HirDecl* Checker::checkMethodDecl(AstNode* node) {
    auto& amethod = node->an_Method;
    auto* bind_type = checkTypeLabel(amethod.bind_type, false);

    auto* uw_bind_type = bind_type->FullUnwrap();
    if (uw_bind_type->kind == TYPE_STRUCT) {
        for (auto& field : uw_bind_type->ty_Struct.fields) {
            if (field.name == amethod.name) {
                fatal(amethod.name_span, "type {} already has field named {}", bind_type->ToString(), field.name);
            }
        }
    } 

    auto& mtable = getMethodTable(bind_type);
    if (mtable.contains(amethod.name)) {
        fatal(amethod.name_span, "type {} has multiple methods named {}", bind_type->ToString(), amethod.name);
    }    

    std::vector<Symbol*> params;
    auto* func_type = checkFuncSignature(amethod.func_type, params);

    auto* method = arena.New<Method>(
        mod.id,
        amethod.name,
        func_type,
        amethod.exported
    );
    mtable.emplace(amethod.name, method);

    auto* hmethod = allocDecl(HIR_METHOD, node->span);
    hmethod->ir_Method.bind_type = bind_type;
    hmethod->ir_Method.method = method;
    hmethod->ir_Method.params = arena.MoveVec(std::move(params));
    hmethod->ir_Method.return_type = func_type->ty_Func.return_type;
    hmethod->ir_Method.body = nullptr;
    return hmethod;
}

HirDecl* Checker::checkFactoryDecl(AstNode* node) {
    auto& afact = node->an_Factory;
    auto* bind_type = checkTypeLabel(afact.bind_type, false);

    Assert(bind_type->kind == TYPE_NAMED || bind_type->kind == TYPE_ALIAS, "non-named method bind type");

    if (bind_type->ty_Named.factory != nullptr) {
        fatal(afact.bind_type->span, "multiple factory functions defined for type {}", bind_type->ToString());
    }

    std::vector<Symbol*> params;
    auto* func_type = checkFuncSignature(afact.func_type, params);

    auto* factory = arena.New<FactoryFunc>(
        mod.id,
        func_type,
        afact.exported
    );
    bind_type->ty_Named.factory = factory;

    auto* hfact = allocDecl(HIR_FACTORY, node->span);
    hfact->ir_Factory.bind_type = bind_type;
    hfact->ir_Factory.func = factory;
    hfact->ir_Factory.params = arena.MoveVec(std::move(params));
    hfact->ir_Factory.return_type = func_type->ty_Func.return_type;
    hfact->ir_Factory.body = nullptr;
    return hfact;
}

Type* Checker::checkFuncSignature(AstNode* node, std::vector<Symbol*>& params) {
    auto& afunc_type = node->an_TypeFunc;

    std::vector<Type*> param_types;
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

    Type* return_type;
    if (afunc_type.return_type) {
        return_type = checkTypeLabel(afunc_type.return_type, false);
    } else {
        return_type = &prim_unit_type;
    }

    auto* func_type = allocType(TYPE_FUNC);
    func_type->ty_Func.param_types = arena.MoveVec(std::move(param_types));
    func_type->ty_Func.return_type = return_type;
    return func_type;
}

MethodTable& Checker::getMethodTable(Type* bind_type) {
    Assert(bind_type->kind == TYPE_NAMED || bind_type->kind == TYPE_ALIAS, "non-named method bind type");

    if (bind_type->ty_Named.methods == nullptr) {
        auto mnode = std::make_unique<Module::MtableNode>();
        auto* mtable = &mnode->mtable;

        if (bind_type->ty_Named.mod_id == mod.id) {
            mnode->next = std::move(mod.mtable_list);
            mod.mtable_list = std::move(mnode);
        } else {
            Module* dep_mod = nullptr;
            for (auto& dep : mod.deps) {
                if (dep.id == mod.id) {
                    dep_mod = dep.mod;
                    break;
                }
            }

            mnode->next = std::move(dep_mod->mtable_list);
            dep_mod->mtable_list = std::move(mnode);
        }

        bind_type->ty_Named.methods = mtable;
    }

    return *bind_type->ty_Named.methods;
}

/* -------------------------------------------------------------------------- */

HirDecl* Checker::checkGlobalVar(AstNode* node) {
    auto* type = checkTypeLabel(node->an_Var.type, false);
    auto* symbol = node->an_Var.symbol;
    symbol->type = type;

    auto* hvar = allocDecl(HIR_GLOBAL_VAR, node->span);
    hvar->ir_GlobalVar.symbol = symbol;
    
    return hvar;
}

HirDecl* Checker::checkGlobalConst(AstNode* node) {
    auto* type = checkTypeLabel(node->an_Var.type, true);
    auto* symbol = node->an_Var.symbol;
    symbol->type = type;

    ConstValue* value;
    if (node->an_Var.init) {
        comptime_depth++;
        auto* hinit = checkExpr(node->an_Var.init, type);
        comptime_depth--;
        
        hinit = subtypeCast(hinit, type);
        finishExpr();

        value = evalComptime(hinit);
    } else {
        value = getComptimeNull(type);
    }
    

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

HirStmt* Checker::checkFuncBody(AstNode* body, std::span<Symbol*> params, Type* return_type) {
    pushScope();
    for (auto* param : params) {
        declareLocal(param);
    }

    enclosing_return_type = return_type;
    auto [hbody, always_returns] = checkStmt(body);
    enclosing_return_type = nullptr;

    popScope();

    if (return_type->kind != TYPE_UNIT && !always_returns) {
        error(hbody->span, "function must return a value");
    }

    return hbody;
}

void Checker::checkMethodBody(Decl* decl) {
    checkMethodAttrs(decl);
    auto& hm = decl->hir_decl->ir_Method;

    pushScope();

    auto* self_ptr_type = allocType(TYPE_PTR);
    self_ptr_type->ty_Ptr.elem_type = hm.bind_type;
    auto* self_ptr = arena.New<Symbol>(
        mod.id,
        "self",
        decl->ast_decl->an_Method.name_span,
        SYM_VAR,
        0,
        self_ptr_type,
        false
    );
    declareLocal(self_ptr);
    hm.self_ptr = self_ptr;

    for (auto* param : hm.params) {
        if (param->name == "self") {
            fatal(param->span, "method cannot have a parameter named self");
        }

        declareLocal(param);
    }

    enclosing_return_type = hm.return_type;
    auto [hbody, always_returns] = checkStmt(decl->ast_decl->an_Method.body);
    enclosing_return_type = nullptr;

    popScope();

    if (hm.return_type->kind != TYPE_UNIT && !always_returns) {
        error(hbody->span, "method must return a value");
    }
    
    hm.body = hbody;
}

void Checker::checkGlobalVarInit(Decl* decl) {
    checkGlobalVarAttrs(decl);

    if (decl->ast_decl->an_Var.init) {
        auto& hgvar = decl->hir_decl->ir_GlobalVar;

        is_comptime_expr = true;
        auto* hexpr = checkExpr(decl->ast_decl->an_Var.init, hgvar.symbol->type);
        hexpr = subtypeCast(hexpr, hgvar.symbol->type);
        finishExpr();

        hgvar.init = hexpr;
        hgvar.const_init = is_comptime_expr ? evalComptime(hexpr) : nullptr;
    }
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

                tag_counter = checkComptimeSize(variant->an_NamedInit.init);
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

            if (named_type->ty_Named.type == nullptr) {
                if (first_pass && (comptime_depth > 0 || should_expand)) {
                    decl_number_stack.push_back(curr_decl_number);
                    curr_decl_number = symbol->decl_number;

                    checkDecl(mod.unsorted_decls[curr_decl_number]);

                    curr_decl_number = decl_number_stack.back();
                    decl_number_stack.pop_back();

                    src_file = &mod.files[mod.unsorted_decls[curr_decl_number]->file_number];
                }
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

        auto* symbol = mustFindSymbolInDep(*dep, node->an_Sel.field_name, node->span);

        if (symbol->flags & SYM_TYPE) {
            return symbol->type;
        } else {
            fatal(node->span, "cannot use a value as a type");
        }
    } break;
    default:
        fatal(node->span, "expected a type label");
    }

    return nullptr;
}

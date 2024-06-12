#include "checker.hpp"

Checker::Checker(Arena& arena, Module& mod)
: arena(arena)
, mod(mod)
, sorted_decls(mod.decls.size())
, init_graph(mod.decls.size())
{
    if (mod.deps.size() > 0) {
        // Core module is always the last dependency added to any module. All
        // modules depend on it implicitly (even if they don't use any symbols
        // from it).
        core_dep = &mod.deps.back();
    } else {
        core_dep = nullptr;
    }
}

void Checker::CheckModule() {
    // First checking pass.
    curr_decl_num = 0;
    for (auto* decl : mod.decls) {
        checkDecl(decl);
        curr_decl_num++;
    }

    // Second checking pass.
    first_pass = false;
    curr_decl_num = 0;
    for (auto* decl : mod.decls) {
        src_file = &mod.files[decl->file_num];

        // Reset colors for init ordering.
        decl->color = COLOR_WHITE;

        // Handle unsafe decls.
        unsafe_depth = (int)((decl->flags & DECL_UNSAFE) > 0);

        switch (decl->hir_decl->kind) {
        case HIR_FUNC:
            if (decl->ast_decl->an_Func.body) {
                decl->hir_decl->ir_Func.body = checkFuncBody(
                    decl->ast_decl->an_Func.body,
                    decl->hir_decl->ir_Func.params,
                    decl->hir_decl->ir_Func.return_type
                );
            }
            break;
        case HIR_METHOD:
            checkMethodBody(decl);
            break;
        case HIR_FACTORY:
            decl->hir_decl->ir_Factory.body = checkFuncBody(
                decl->ast_decl->an_Factory.body,
                decl->hir_decl->ir_Factory.params,
                decl->hir_decl->ir_Factory.return_type
            );
            break;
        }

        unsafe_depth = 0;
        curr_decl_num++;
    }

    // Sort remaining declarations into correct initialization order.
    curr_decl_num = 0;
    for (auto* decl : mod.decls) {
        switch (decl->hir_decl->kind) {
        case HIR_FUNC: case HIR_METHOD: case HIR_FACTORY:
            if (decl->color == COLOR_WHITE) {
                sorted_decls[n_sorted++] = decl;
            }
            break;
        case HIR_GLOBAL_VAR:
            addToInitOrder(decl);
            break;
        }

        curr_decl_num++;
    }

    // Replace the old unsorted decl vector with the sorted version.
    mod.decls = std::move(sorted_decls);

    // Update the declaration numbers of the newly sorted declarations.
    curr_decl_num = 0;
    for (auto* decl : mod.decls) {
        switch (decl->hir_decl->kind) {
        case HIR_GLOBAL_VAR:
            decl->hir_decl->ir_GlobalVar.symbol->decl_num = curr_decl_num;
            break;
        case HIR_GLOBAL_CONST:
            decl->hir_decl->ir_GlobalConst.symbol->decl_num = curr_decl_num;
            break;
        case HIR_FUNC:
            decl->hir_decl->ir_Func.symbol->decl_num = curr_decl_num;
            break;
        case HIR_STRUCT:
        case HIR_ENUM:
        case HIR_ALIAS:
            decl->hir_decl->ir_TypeDef.symbol->decl_num = curr_decl_num;
            break;
        case HIR_METHOD:
            decl->hir_decl->ir_Method.method->decl_num = curr_decl_num;
            break;
        case HIR_FACTORY:
            decl->hir_decl->ir_Factory.bind_type->ty_Named.factory->decl_num = curr_decl_num;
            break;
        }

        curr_decl_num++;
    }
}

/* -------------------------------------------------------------------------- */

static Symbol* getDeclSymbol(Decl* decl) {
    auto* node = decl->ast_decl;
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

static std::pair<std::string, bool> getDeclNameAndConst(Decl* decl) {
    // NOTE: Methods and factories can only be involved in cycles during init
    // order checking, so we are safe to access their HIR nodes.
    switch (decl->ast_decl->kind) {
    case HIR_METHOD:
        return { std::string(decl->hir_decl->ir_Method.method->name), false };
    case HIR_FACTORY:
        return { decl->hir_decl->ir_Factory.bind_type->ToString() + ".factory", false };
    default: {
        auto* symbol = getDeclSymbol(decl);
        return { std::string(symbol->name), symbol->flags & SYM_CONST };
    } break;
    }
}

bool Checker::addToInitOrder(Decl* decl) {
    switch (decl->color) {
    case COLOR_BLACK:
        break;
    case COLOR_WHITE: {
        decl->color = COLOR_GREY;

        auto& edges = init_graph[curr_decl_num];
        pushDeclNum(0);  // 0 is just a dummy argument.

        for (size_t edge_number : edges) {
            curr_decl_num = edge_number;
            if (addToInitOrder(mod.decls[curr_decl_num])) {
                decl->color = COLOR_BLACK;
                return true;
            }
        }

        popDeclNum();

        sorted_decls[n_sorted++] = decl;
        decl->color = COLOR_BLACK;
        return false;
    } break;
    case COLOR_GREY:
        // We might have an invalid cycle at this point, but we have to check.
        // Cycles involving only functions are fine (recursion).  It's when we
        // have cycles that involve global variables that there is a problem.
        for (size_t i = decl_num_stack.size() - 1; i >= 0; i--) {
            auto decl_num = decl_num_stack[i];

            // If a global variable is part of the cycle, then the execution of
            // its initializer in some way depends on the evaluation of a
            // function which depends recursively upon its value. 
            if (mod.decls[decl_num]->hir_decl->kind == HIR_GLOBAL_VAR) {
                reportCycle(decl);
                decl->color = COLOR_BLACK;
                return true;
            }

            if (decl_num == curr_decl_num) {
                break;
            }
        } 
        break;
    }

    return false;
}

void Checker::reportCycle(Decl* decl) {
    auto* start_symbol = getDeclSymbol(decl);
    std::string fmt_cycle(start_symbol->name);
    bool cycle_involves_const = false;
    
    for (size_t i = decl_num_stack.size() - 1; i >= 0; i--) {
        auto n = decl_num_stack[i];

        fmt_cycle += " -> ";

        auto [name, is_const] = getDeclNameAndConst(mod.decls[n]);
        fmt_cycle += name;
        if (is_const) {
            cycle_involves_const = true;
        }

        if (n == curr_decl_num) {
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

void Checker::mustEqual(const TextSpan &span, Type* a, Type* b) {
    tctx.infer_enabled = true;

    if (!tctx.Equal(a, b)) {
        fatal(span, "type mismatch: {} v. {}", a->ToString(), b->ToString());
    }

    tctx.infer_enabled = false;
}

bool Checker::mustSubType(const TextSpan &span, Type* sub, Type* super) {
    tctx.infer_enabled = true;

    auto conv_result = tctx.SubType(sub, super);
    if (conv_result == TY_CONV_FAIL) {
        fatal(span, "{} is not a subtype of {}", sub->ToString(), super->ToString());
    }

    tctx.infer_enabled = false;

    return conv_result == TY_CONV_CAST;
}

void Checker::mustCast(const TextSpan &span, Type* src, Type* dest) {
    tctx.infer_enabled = true;
    tctx.unsafe_enabled = unsafe_depth > 0;

    if (!tctx.Cast(src, dest)) {
        fatal(span, "{} cannot be cast to {}", src->ToString(), dest->ToString());
    }

    tctx.infer_enabled = false;
}

void Checker::mustIntType(const TextSpan& span, Type* type) {
    tctx.infer_enabled = true;

    if (!tctx.IsIntType(type)) {
        fatal(span, "expected an integer type but got {}", type->ToString());
    }

    tctx.infer_enabled = false;
}

Type* Checker::newUntyped(UntypedKind kind) {
    auto* untyped = AllocType(arena, TYPE_UNTYP);
    tctx.AddUntyped(untyped, kind);
    return untyped;
}

void Checker::finishExpr() {
    tctx.InferAll();
    tctx.Clear();

    bool any_bad_nulls = false;
    for (auto& pair : null_spans) {
        if (pair.untyped->ty_Untyp.concrete_type == nullptr) {
            error(pair.span, "unable to infer type of null");
            any_bad_nulls = true;
        }
    }

    null_spans.clear();

    if (any_bad_nulls)
        throw CompileError{};
}

HirExpr* Checker::createImplicitCast(HirExpr* src, Type* dest_type) {
    auto* hcast = allocExpr(HIR_CAST, src->span);
    hcast->type = dest_type;
    hcast->ir_Cast.expr = src;
    return hcast;
}

HirExpr* Checker::subtypeCast(HirExpr* src, Type* dest_type) {
    if (mustSubType(src->span, src->type, dest_type)) {
        return createImplicitCast(src, dest_type);
    }

    return src;
}

/* -------------------------------------------------------------------------- */

std::pair<Symbol*, Module::DepEntry*> Checker::mustLookup(std::string_view name, const TextSpan& span) {
    for (int i = scope_stack.size() - 1; i >= 0; i--) {
        auto& scope = scope_stack[i];

        auto it = scope.find(name);
        if (it != scope.end()) {
            return { it->second, nullptr };
        }
    }

    auto dep_it = src_file->import_table.find(name);
    if (dep_it != src_file->import_table.end()) {
        return { nullptr, &mod.deps[dep_it->second] };
    }

    auto it = mod.symbol_table.find(name);
    if (it != mod.symbol_table.end()) {
        if ((it->second->flags & SYM_COMPTIME) == 0) {
            init_graph[curr_decl_num].insert(it->second->decl_num);
        }

        return { it->second, nullptr };
    }

    if (core_dep != nullptr) {
        auto* symbol = findSymbolInDep(*core_dep, name);
        if (symbol != nullptr) {
            return { symbol, nullptr };
        }
    }

    fatal(span, "undefined symbol: {}", name);
    return {};
}

Symbol* Checker::findSymbolInDep(Module::DepEntry& dep, std::string_view name) {
    auto it = dep.mod->symbol_table.find(name);
    if (it == dep.mod->symbol_table.end()) {
        return nullptr;
    }

    auto* imported_symbol = it->second;
    if (imported_symbol->flags & SYM_EXPORTED) {
        dep.usages.insert(imported_symbol->decl_num);
        return imported_symbol;
    }

    return nullptr;
}

Symbol* Checker::mustFindSymbolInDep(Module::DepEntry& dep, std::string_view name, const TextSpan& span) {
    auto* imported_symbol = findSymbolInDep(dep, name);
    if (imported_symbol == nullptr) {
        fatal(span, "module {} has no exported symbol named {}", dep.mod->name, name);
    }

    return imported_symbol;
}

/* -------------------------------------------------------------------------- */

Method* Checker::tryLookupMethod(const TextSpan& span, Type* bind_type, std::string_view method_name) {
    if (bind_type->kind == TYPE_NAMED || bind_type->kind == TYPE_ALIAS) {
        if (bind_type->ty_Named.methods != nullptr) {
            auto it = bind_type->ty_Named.methods->find(method_name);
            if (it != bind_type->ty_Named.methods->end()) {
                auto* method = it->second;

                if (method->parent_id != mod.id) {
                    if (method->exported) {
                        // Add the appropriate usage entry.
                        for (auto& dep : mod.deps) {
                            if (dep.mod->id == method->parent_id) {
                                dep.usages.insert(method->decl_num);
                                break;
                            }
                        }
                    } else {
                        fatal(span, "method {} of type {} is not exported", method_name, bind_type->ToString());
                    }
                } else if (!first_pass) { // Do we even need this check?
                    init_graph[curr_decl_num].insert(method->decl_num);
                }

                return method;
            }
        }
    }

    return nullptr;
}

/* -------------------------------------------------------------------------- */

void Checker::declareLocal(Symbol* sym) {
    Assert(scope_stack.size() > 0, "declare local on empty scope stack");

    auto& curr_scope = scope_stack.back();

    if (!curr_scope.contains(sym->name)) {
        curr_scope[sym->name] = sym;
    } else {
        fatal(sym->span, "multiple definitions of local variable {} in the same scope", sym->name);
    }
}

void Checker::pushScope() {
    scope_stack.push_back({});
}

void Checker::popScope() {
    Assert(scope_stack.size() > 0, "pop on empty scope stack");

    scope_stack.pop_back();
}

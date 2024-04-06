#include "resolver.hpp"

class Resolver {
    Arena& arena;
    Module& mod;
    Module::DepEntry* core_dep;

    std::vector<GColor> colors;
    std::vector<size_t> cycle_nodes;
    bool cycle_done { false };

public:
    Resolver(Arena& arena_, Module& mod_)
    : arena(arena_)
    , mod(mod_)
    , colors(mod_.unsorted_decls.size(), COLOR_WHITE)
    {
        if (mod.deps.size() > 0)
            core_dep = &mod.deps.back();
        else
            core_dep = nullptr;
    }

    void Resolve() {
        size_t n = 0;
        for (; n < mod.unsorted_decls.size(); n++) {
            if (resolveDecl(n)) {
                errorOnCycle();
            }
        }

        // NOTE: The below algorithm while seeming more complex and inefficient
        // is actually substantially faster than just calling erase over and
        // over again on unsorted_decls.  Erase has to shift the unsorted decls
        // backwards every time it is called which is O(M + N) where M is the
        // number of sorted and N is the number of unsorted.  This has to happen
        // for every sorted decl which thus gives a complexity of O(M(N+M))
        // which is quadratic.  By contrast, the "ugly" algorithm below only
        // makes one pass over the sorted list and one pass over the unsorted
        // list performing constant time operations at each step.  Hence, its
        // complexity is O(M + N) aka linear :)

        // Also notice that I deliberately avoid updating the decl numbers until
        // after the sorting is done.  This saves a major headache during
        // resolution of trying to keep track of which decl numbers are valid
        // and which ones have been moved around.  Trust me on this one...

        // Update the decl numbers of the newly sorted decls and create "holes"
        // for them in the unsorted decl list.
        n = 0;
        for (auto* decl : mod.sorted_decls) {
            size_t old_decl_number;
            switch (decl->ast_decl->kind) {
            case AST_TYPEDEF:
                old_decl_number = decl->ast_decl->an_TypeDef.symbol->decl_number;
                decl->ast_decl->an_TypeDef.symbol->decl_number = n;
                break;
            case AST_CONST:
                old_decl_number = decl->ast_decl->an_Var.symbol->decl_number;
                decl->ast_decl->an_Var.symbol->decl_number = n;
                break;
            }

            mod.unsorted_decls[old_decl_number] = nullptr;

            n++;
        }

        // Fill in the holes by shifting around the unsorted decls updating
        // their decl numbers as we go.  Trim all the empty space off the end.
        size_t p = 0;
        for (n = mod.unsorted_decls.size() - 1; n >= p;) {
            auto* decl = mod.unsorted_decls[n];

            if (decl == nullptr) {
                mod.unsorted_decls.pop_back();
                n--;
                continue;
            } 

            mod.unsorted_decls[n] = mod.unsorted_decls[p];
            mod.unsorted_decls[p] = decl;

            switch (decl->ast_decl->kind) {
            case AST_VAR:
                decl->ast_decl->an_Var.symbol->decl_number = p;
                break;
            case AST_FUNC:
                decl->ast_decl->an_Func.symbol->decl_number = p;
                break;
            }

            p++;
        }
    }

private:
    bool resolveDecl(size_t decl_number) {
        auto* decl = mod.unsorted_decls[decl_number];
        auto* node = decl->ast_decl;

        switch (node->kind) {
        case AST_VAR: case AST_FUNC:
            return false;
        }

        switch (colors[decl_number]) {
        case COLOR_BLACK:
            return false;
        case COLOR_GREY:
            cycle_nodes.push_back(decl_number);
            return true;
        }

        colors[decl_number] = COLOR_GREY;
        bool has_cycle;
        if (node->kind == AST_TYPEDEF) {
            has_cycle = resolveTypeLabel(decl, node->an_TypeDef.type);
        } else {
            has_cycle = resolveTypeLabel(decl, node->an_Var.type);

            if (!has_cycle)
                has_cycle = resolveExpr(decl, node->an_Var.init);
        } 

        colors[decl_number] = COLOR_BLACK;

        if (has_cycle) {
            if (!cycle_done) {
                cycle_nodes.push_back(decl_number);

                if (cycle_nodes[0] == decl_number) {
                    cycle_done = true;
                }
            }

            return true;
        } else {
            mod.sorted_decls.push_back(decl);
            return false;
        }
    }

    /* ---------------------------------------------------------------------- */

    bool resolveTypeLabel(Decl* decl, AstNode* node, bool should_expand = true) {
        switch (node->kind) {
        case AST_TYPE_PRIM:
            return false;
        case AST_TYPE_ARRAY: 
            // Arrays always have to contain at least one element, so they count
            // as hard links between types.  For example:
            //
            //  struct Struct {
            //      arr: [5]Struct;
            //  }
            // 
            // would be an infinite type.

            if (resolveTypeLabel(decl, node->an_TypeArray.elem_type))
                return true;

            // We always have to expand array sizes even if should_expand == false.  
            return resolveExpr(decl, node->an_TypeArray.len);
        case AST_TYPE_SLICE:            
            return resolveTypeLabel(decl, node->an_TypeArray.elem_type, false);
        case AST_TYPE_FUNC:
            for (auto& param : node->an_TypeFunc.params) {
                if (resolveTypeLabel(decl, param.type, false))
                    return true;
            }

            return resolveTypeLabel(decl, node->an_TypeFunc.return_type, false);
        case AST_TYPE_ENUM:
            for (auto* variant : node->an_TypeEnum.variants) {
                if (variant->kind == AST_NAMED_INIT && resolveExpr(decl, variant->an_NamedInit.init)) {
                    return true;
                }
            }

            return false;
        case AST_TYPE_STRUCT: 
            for (auto& field : node->an_TypeStruct.fields) {
                if (resolveTypeLabel(decl, field.type))
                    return true;
            }
            return false;
        case AST_IDENT: {
            Symbol* symbol = nullptr;
            auto it = mod.symbol_table.find(node->an_Ident.name);
            if (it == mod.symbol_table.end()) {
                if (core_dep != nullptr) {
                    symbol = lookupSymbolInDep(*core_dep, node->an_Ident.name);
                }
            } else {
                symbol = it->second;
            }

            if (symbol == nullptr) {
                error(decl, node->span, "undefined symbol: {}", node->an_Ident.name);
                return false;
            }

            node->an_Ident.symbol = symbol;

            if (symbol->flags & SYM_TYPE && symbol->parent_id == mod.id) {
                if (should_expand) {
                    return resolveDecl(symbol->decl_number);
                }
            } else {
                error(decl, node->span, "cannot use value as a type");
            }
        } break;
        case AST_SELECTOR: {
            auto& src_file = mod.files[decl->file_number];

            auto* ident = node->an_Sel.expr;
            auto it = src_file.import_table.find(ident->an_Ident.name);
            if (it == src_file.import_table.end()) {
                error(decl, node->span, "file has no import named {}", ident->an_Ident.name);
                return false;
            }

            auto& dep = mod.deps[it->second];
            auto* imported_symbol = lookupSymbolInDep(dep, node->an_Sel.field_name);
            if (imported_symbol == nullptr) {
                error(decl, node->span, "module {} has no exported symbol named {}", dep.mod->name, node->an_Sel.field_name);
                return false;
            }

            if (imported_symbol->flags & SYM_TYPE) {
                // This is always ok since sizeof(AST_SELECTOR) > sizeof(AST_STATIC_GET)
                node->kind = AST_STATIC_GET;
                node->an_StaticGet.imported_symbol = imported_symbol;
            } else {
                error(decl, node->span, "cannot use value as a type");
            }
        } break;
        case AST_DEREF: 
            return resolveTypeLabel(decl, node->an_Deref.expr, false);
        default:
            Panic("unsupported type label in resolver");
            return false;
        }
        
    }

    Symbol* lookupSymbolInDep(Module::DepEntry& dep, std::string_view name) {
        auto it = dep.mod->symbol_table.find(name);
        if (it == dep.mod->symbol_table.end()) {
            return nullptr;
        }

        auto* symbol = it->second;
        if (symbol->flags & SYM_EXPORTED) {
            return symbol;
        } 

        return nullptr;
    }

    /* ---------------------------------------------------------------------- */

    bool resolveExpr(Decl* decl, AstNode* node) {
        switch (node->kind) {
        case AST_CAST:
            return resolveExpr(decl, node->an_Cast.expr) 
                || resolveTypeLabel(decl, node->an_Cast.dest_type);
        case AST_BINOP:
            return resolveExpr(decl, node->an_Binop.lhs)
                || resolveExpr(decl, node->an_Binop.rhs);
        case AST_UNOP:
            return resolveExpr(decl, node->an_Unop.expr);
        case AST_INDEX:
            return resolveExpr(decl, node->an_Index.expr)
                || resolveExpr(decl, node->an_Index.index);
        case AST_SLICE:
            return resolveExpr(decl, node->an_Slice.expr)
                || resolveExpr(decl, node->an_Slice.start_index)
                || resolveExpr(decl, node->an_Slice.end_index);
        case AST_ARRAY_LIT:
            for (auto* item : node->an_ExprList.exprs) {
                if (resolveExpr(decl, item)) {
                    return true;
                }
            }

            return false;
        case AST_STRUCT_LIT:
            for (auto* field_init : node->an_StructLit.field_inits) {
                if (field_init->kind == AST_NAMED_INIT) {
                    field_init = field_init->an_NamedInit.init;
                }

                if (resolveExpr(decl, field_init)) {
                    return true;
                }
            }

            return false;
        case AST_IDENT: {
            auto* dep = lookupNameOrDep(decl, node);
            auto* symbol = node->an_Ident.symbol;

            if (dep != nullptr) {
                error(decl, node->span, "cannot use module as a value");
                return false;
            } else if (symbol == nullptr) {
                return false;
            }

            if (symbol->flags & SYM_TYPE) {
                error(decl, node->span, "cannot use type as a value");
            } else if (symbol->flags & SYM_COMPTIME && symbol->parent_id == mod.id) {
                return resolveDecl(symbol->decl_number);
            } else {
                error(decl, node->span, "expression cannot be evaluated at compile time");
            }
        } break;
        case AST_SELECTOR: 
            if (node->an_Sel.expr->kind == AST_IDENT) {
                auto ident = node->an_Sel.expr;

                auto* dep = lookupNameOrDep(decl, ident);
                auto* symbol = ident->an_Ident.symbol;
                
                if (dep != nullptr) {
                    auto* imported_symbol = lookupSymbolInDep(*dep, node->an_Sel.field_name);
                    if (imported_symbol == nullptr) {
                        error(decl, node->span, "module {} has no exported symbol named {}", dep->mod->name, node->an_Sel.field_name);
                    } else {
                        node->kind = AST_STATIC_GET;
                        node->an_StaticGet.imported_symbol = imported_symbol;
                    }

                    return false;
                } else if (symbol == nullptr || symbol->parent_id != mod.id) {
                    return false;
                }

                if (symbol->flags & SYM_TYPE || symbol->flags & SYM_COMPTIME) {
                    return resolveDecl(symbol->decl_number);
                } 

                return false;
            }

            return resolveExpr(decl, node->an_Sel.expr);
        case AST_NUM_LIT:
        case AST_FLOAT_LIT:
        case AST_BOOL_LIT:
        case AST_RUNE_LIT:
        case AST_STRING_LIT:
        case AST_NULL:
            return false;
        default:
            // TODO: handle comptime matching
            error(decl, node->span, "expression cannot be evaluated at compile time");
            break;
        }

        return false;
    }

    Module::DepEntry* lookupNameOrDep(Decl* decl, AstNode* ident) {
        auto it = mod.symbol_table.find(ident->an_Ident.name);
        if (it == mod.symbol_table.end()) {
            auto& import_table = mod.files[decl->file_number].import_table;
            auto dep_it = import_table.find(ident->an_Ident.name);
            if (dep_it != import_table.end()) {
                return &mod.deps[dep_it->second];
            }

            ident->an_Ident.symbol = lookupSymbolInDep(*core_dep, ident->an_Ident.name);
        } else {
            ident->an_Ident.symbol = it->second;
        }

        return nullptr;
    }

    /* ---------------------------------------------------------------------- */

    void errorOnCycle() {
        std::string fmt_cycle;

        Decl* start_decl = nullptr;
        TextSpan start_span;
        bool contains_const = false;
        
        while (cycle_nodes.size() > 0) {
            auto decl_number = cycle_nodes.back();
            cycle_nodes.pop_back();

            auto* decl = mod.unsorted_decls[decl_number];

            Symbol* symbol;
            switch (decl->ast_decl->kind) {
            case AST_TYPEDEF:
                symbol = decl->ast_decl->an_TypeDef.symbol;
                break;
            case AST_CONST:
                symbol = decl->ast_decl->an_Var.symbol;
                contains_const = true;
                break;
            default:
                Panic("invalid decl in resolver cycle printing");
                continue;
            }

            if (start_decl == nullptr) {
                start_decl = decl;
                start_span = symbol->span;
            } else {
                fmt_cycle += " -> ";
            }

            fmt_cycle += symbol->name;
        }

        if (start_decl->ast_decl->kind == AST_TYPEDEF) {
            if (contains_const) {
                error(start_decl, start_span, "type definition depends cyclically on constant: {}", fmt_cycle);
            } else {
                error(start_decl, start_span, "infinite type: {}", fmt_cycle);
            }
        } else {
            error(start_decl, start_span, "initialization cycle detected: {}", fmt_cycle);
        }
    }

    template<typename... Args>
    void error(Decl* decl, const TextSpan& span, const std::string& message, Args&&... args) {
        ReportCompileError(mod.files[decl->file_number].display_path, span, message, args...);
    }
};


/* -------------------------------------------------------------------------- */

void Resolve(Arena& arena, Module& mod) {
    Resolver r(arena, mod);
    r.Resolve();
}
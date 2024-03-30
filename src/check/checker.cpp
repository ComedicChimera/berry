#include "checker.hpp"

Checker::Checker(Arena& arena, Module& mod)
: arena(arena)
, mod(mod)
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
    for (auto* def : mod.defs) {
        src_file = &mod.files[def->parent_file_number];
        init_graph.emplace_back();

        checkDef(def);
    }

    size_t def_number = 0;
    for (auto* def : mod.defs) {
        if (def->kind == AST_GLVAR || def->kind == AST_ENUM) {
            InitCycle cycle;
            if (checkInitOrder(def_number, cycle)) {
                auto* cycle_start = mod.defs[cycle.nodes[0]];

                std::string fmt_cycle;
                while (cycle.nodes.size() > 0) {
                    if (fmt_cycle.size() > 0) {
                        fmt_cycle += " -> ";
                    }

                    auto node = cycle.nodes.back();
                    cycle.nodes.pop_back();

                    auto* def = mod.defs[node];
                    switch (def->kind) {
                    case AST_FUNC:
                        fmt_cycle += def->an_Func.symbol->name;
                        break;
                    case AST_GLVAR:
                        fmt_cycle += def->an_GlVar.symbol->name;
                        break;
                    case AST_STRUCT:
                        fmt_cycle += def->an_Struct.symbol->name;
                        break;
                    }
                }

                src_file = &mod.files[cycle_start->parent_file_number];
                error(cycle_start->span, "initialization cycle detected: {}", fmt_cycle);
            }
        }

        def_number++;
    }
}

/* -------------------------------------------------------------------------- */

bool Checker::checkInitOrder(size_t def_number, InitCycle& cycle) {
    auto& node = init_graph[def_number];
    auto* def = mod.defs[def_number];

    switch (node.color) {
    case COLOR_BLACK:
        return false;
    case COLOR_GREY:
        node.color = COLOR_BLACK;

        if (def->kind == AST_GLVAR || def->kind == AST_ENUM) {
            cycle.nodes.push_back(def_number);
            return true;    
        }

        break;
    case COLOR_WHITE:
        node.color = COLOR_GREY;

        for (auto edge : node.edges) {
            if (checkInitOrder(edge, cycle)) {
                if (!cycle.done) {
                    if (cycle.nodes[0] == def_number) {
                        cycle.done = true;
                    }

                    cycle.nodes.push_back(def_number);
                }
                
                node.color = COLOR_BLACK;
                return true;
            }
        }

        if (def->kind == AST_GLVAR || def->kind == AST_ENUM) {
            mod.init_order.push_back(def_number);
        }

        node.color = COLOR_BLACK;
        break;
    }
    
    return false;
}

/* -------------------------------------------------------------------------- */

void Checker::mustEqual(const TextSpan &span, Type* a, Type* b) {
    tctx.flags |= TC_INFER;

    if (!tctx.Equal(a, b)) {
        fatal(span, "type mismatch: {} v. {}", a->ToString(), b->ToString());
    }

    tctx.flags ^= TC_INFER;
}

void Checker::mustSubType(const TextSpan &span, Type* sub, Type* super) {
    tctx.flags |= TC_INFER;

    if (!tctx.SubType(sub, super)) {
        fatal(span, "{} is not a subtype of {}", sub->ToString(), super->ToString());
    }

    tctx.flags ^= TC_INFER;
}

void Checker::mustCast(const TextSpan &span, Type* src, Type* dest) {
    tctx.flags |= TC_INFER;

    if (!tctx.Cast(src, dest)) {
        fatal(span, "{} cannot be cast to {}", src->ToString(), dest->ToString());
    }

    tctx.flags ^= TC_INFER;
}

void Checker::mustIntType(const TextSpan& span, Type* type) {
    tctx.flags |= TC_INFER;

    if (!tctx.IsIntType(type)) {
        fatal(span, "expected an integer type but got {}", type->ToString());
    }

    tctx.flags ^= TC_INFER;
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
        if (pair.first->ty_Untyp.concrete_type == nullptr) {
            error(pair.second, "unable to infer type of null");
            any_bad_nulls = true;
        }
    }

    null_spans.clear();

    if (any_bad_nulls)
        throw CompileError{};
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

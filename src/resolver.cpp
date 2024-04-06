#include "resolver.hpp"

class Resolver {
    Arena& arena;
    Module& mod;

public:
    Resolver(Arena& arena_, Module& mod_)
    : arena(arena_)
    , mod(mod_)
    {}

    void Resolve() {
        size_t n = 0;
        for (auto* decl : mod.unsorted_decls) {
            switch (decl->ast_decl->kind) {
            case AST_TYPEDEF:
                resolveTypeDef(n, decl->ast_decl);
                break;
            case AST_CONST:
                resolveConst(n, decl->ast_decl);
                break;
            }

            n++;
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
    void resolveTypeDef(size_t decl_number, AstNode* node) {
        // TODO
    }

    /* ---------------------------------------------------------------------- */

    void resolveConst(size_t decl_number, AstNode* node) {
        // TODO
    }
};


/* -------------------------------------------------------------------------- */

void Resolve(Arena& arena, Module& mod) {
    Resolver r(arena, mod);
    r.Resolve();
}
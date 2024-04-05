#ifndef SYMBOL_H_INC
#define SYMBOL_H_INC

#include <unordered_set>

#include "base.hpp"
#include "types.hpp"

// SymbolFlags enumerates the possible flags that can be set on symbols.
enum {
    // Symbol Kinds
    SYM_VAR = 1,    // Variable
    SYM_FUNC = 2,   // Function
    SYM_TYPE = 4,   // Type Definition
    SYM_CONST = 8,  // Constant 

    // Symbol Modifiers
    SYM_EXPORTED = 16,  // Symbol is publically visible

    // Useful Flag Combinations (for condition checking)
    SYM_COMPTIME = SYM_FUNC | SYM_TYPE | SYM_CONST // Symbol is compile-time constant
};
typedef int SymbolFlags;

// Symbol represents a named symbol in a Berry module.
struct Symbol {
    // parent_id the ID of the symbol's parent module.
    size_t parent_id;

    // name is the name of the symbol.
    std::string_view name;

    // span is the source location of the symbol definition.
    TextSpan span;

    // flags are the flags associated with the symbol.
    SymbolFlags flags;

    // def_number is the number of the global definition of the symbol. For
    // local symbols, this field is not used.
    size_t def_number;

    // type is the data type of the symbol.
    Type* type { nullptr };

    // immut indicates whether the symbol is immutable.
    bool immut { false };

    // llvm_value contains the LLVM value bound to the symbol.
    llvm::Value* llvm_value { nullptr };
};

// Attribute represents a Berry definition attribute.
struct Attribute {
    // The name of the tag.
    std::string_view name;

    // The source span containing the tag name.
    TextSpan name_span;

    // The value of the tag (may be empty if no value).
    std::string_view value;

    // The source span containing the value (if it exists).
    TextSpan value_span;
};

/* -------------------------------------------------------------------------- */

struct SourceFile;
struct AstDecl;
struct HirDecl;

// Module represents a Berry module.
struct Module {
    // id is the module's unique ID.
    size_t id;

    // name is the identifying name or path of the module (ex: "main", "io.std")
    std::string name;

    // files is the list of files contained in the module.
    std::vector<SourceFile> files;

    // symbol_table is the module's global symbol table.
    std::unordered_map<std::string_view, Symbol*> symbol_table;

    // Decl is a declaration in the module.
    struct Decl {
        // file_number is the module-local number of the declaring file.
        size_t file_number;

        // ast_decl is the declaration AST node.
        AstDecl* ast_decl;

        // hir_decl is the declaration HIR node.
        HirDecl* hir_decl { nullptr };

        Decl(size_t file_number_, AstDecl* adecl_)
        : file_number(file_number_)
        , ast_decl(adecl_)
        {}
    };

    // decls stores the declarations contained in the module.
    std::vector<Decl> decls;

    // DepEntry is a module dependency entry.
    struct DepEntry {
        // mod is depended upon module.  This will be nullptr until the
        // dependency is resolved by the loader.
        Module* mod { nullptr };

        // mod_path is the Berry path to the module: each dot separated element
        // is its own entry in the mod_path vector.
        std::vector<std::string> mod_path;

        // usages stores the definition numbers exported symbols accessed
        // through this dependency.
        std::unordered_set<size_t> usages;

        // import_locs stores the source location of all imports of this
        // dependency.  This is to simplify error handling.
        std::vector<std::pair<size_t, TextSpan>> import_locs;

        DepEntry(std::vector<std::string>&& mod_path_)
        : mod(nullptr)
        , mod_path(std::move(mod_path_))
        {}

        DepEntry(Module* mod_)
        : mod(mod_)
        , mod_path({ mod_->name })
        {}
    };

    // deps stores the module's dependencies.
    std::vector<DepEntry> deps;
};

/* -------------------------------------------------------------------------- */

// SourceFile represents a single source file in a Berry module.
struct SourceFile {
    // parent is the module the file is apart of.
    Module* parent;

    // file_number uniquely identifies the source file within its parent module.
    size_t file_number;
    
    // abs_path is the absolute path to the file.
    std::string abs_path;

    // display_path is the path displayed to the user to identify the file.
    std::string display_path;
    
    // import_table stores the package's imports.
    std::unordered_map<std::string_view, size_t> import_table;

    // llvm_di_file is the debug info scope associated with this file.
    llvm::DIFile* llvm_di_file { nullptr };

    SourceFile(Module* parent_, size_t file_number_, std::string&& abs_path_, std::string&& display_path_)
    : parent(parent_)
    , file_number(file_number_)
    , abs_path(std::move(abs_path_))
    , display_path(std::move(display_path_))
    , llvm_di_file(nullptr)
    {}

    SourceFile(SourceFile&& src_file)
    : parent(src_file.parent)
    , file_number(src_file.file_number)
    , abs_path(std::move(src_file.abs_path))
    , display_path(std::move(src_file.display_path))
    , llvm_di_file(src_file.llvm_di_file)
    {}
};

#endif
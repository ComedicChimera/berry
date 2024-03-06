#ifndef SYMBOL_H_INC
#define SYMBOL_H_INC

#include <unordered_map>
#include <unordered_set>

#include "base.hpp"
#include "types.hpp"

// Forward Declaration of LLVM fields. 
namespace llvm {
    class Value;

    class DIScope;

    class DIFile;
}

// SymbolFlags enumerates the possible flags that can be set on symbols.
enum {
    SYM_VAR = 0,    // Variable
    SYM_FUNC = 1,   // Function
    SYM_TYPE = 2,   // Type Definition

    SYM_EXPORTED = 4  // Symbol is publically visible
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

    // type is the data type of the symbol.
    Type* type { nullptr };

    // immut indicates whether the symbol is immutable.
    bool immut { false };

    // llvm_value contains the LLVM value bound to the symbol.
    llvm::Value* llvm_value { nullptr };
};

/* -------------------------------------------------------------------------- */

struct SourceFile;
struct AstDef;
struct AstGlobalVar;

// NamedTypeTable stores all the named types used by a given module.  It is an
// extra bit of bookkeeping used to facilitate out-of-order resolution.
struct NamedTypeTable {
    // Ref represents a reference to a named type in source code.  These are
    // resolved by the loader after the parser has been run.
    struct Ref {
        // named_type refers to the named type that is still unresolved.
        Type* named_type;

        // spans is the source locations of the named type reference.
        std::vector<TextSpan> spans;
    };

    // internal_refs are all the refs which refer to types within the current
    // module (or implicitly imported from the core module).
    std::unordered_map<std::string, Ref> internal_refs;

    // external_refs are all the refs to types in different/imported modules
    // (indexed by dependency ID).
    std::vector<std::unordered_map<std::string, Ref>> external_refs;
};


// Module represents a Berry module.
struct Module {
    // id is the module's unique ID.
    size_t id;

    // name is the identifying name or path of the module (ex: "main", "io.std")
    std::string name;

    // files is the list of files contained in the module.
    std::vector<SourceFile> files;

    // global_vars is the global variable ASTs comprising the module.  These are
    // arranged in the order that their initializers should be generated.
    std::vector<AstGlobalVar*> global_vars;

    // SymbolTableEntry is an entry in the module's global symbol table.
    struct SymbolTableEntry {
        // symbol is the symbol contained in the entry.
        Symbol* symbol;

        // file_number and def_number identify the global definition
        // corresponding to the symbol.  For global variables, file_number is
        // unused (set to 0).
        size_t file_number, def_number;
    };

    // symbol_table is the module's global symbol table.
    std::unordered_map<std::string_view, SymbolTableEntry> symbol_table;

    // ImportLoc represents a location where a dependency is imported.
    struct ImportLoc {
        // src_file is the file that performs the import.
        SourceFile& src_file;

        // span is the span of the module path in the import stmt.
        TextSpan span;        
    };

    // Dependency represents a module dependency.
    struct Dependency {
        // mod is depended upon module.  This will be nullptr until the
        // dependency is resolved by the loader.
        Module* mod { nullptr };

        // mod_path is the Berry path to the module: each dot separated element
        // is its own entry in the mod_path vector.
        std::vector<std::string> mod_path;

        // usages stores the exported symbols accesses through this dependency.
        std::unordered_set<std::string_view> usages;

        // import_locs lists the locations where the dependency is imported.
        std::vector<ImportLoc> import_locs;

        Dependency(std::vector<std::string>&& mod_path_, ImportLoc&& loc)
        : mod(nullptr)
        , mod_path(std::move(mod_path_))
        , import_locs({std::move(loc)})
        {}

        Dependency(Module* mod_)
        : mod(mod_)
        , mod_path({ mod_->name })
        {}
    };

    // deps stores the module's dependencies.
    std::vector<Dependency> deps;

    // named_table stores the module's named type dependencies.
    NamedTypeTable named_table;
};

/* -------------------------------------------------------------------------- */

// SourceFile represents a single source file in a Berry module.
struct SourceFile {
    // parent is the module the file is apart of.
    Module* parent;

    // file_number uniquely identifies the file within its parent module.
    size_t file_number;
    
    // abs_path is the absolute path to the file.
    std::string abs_path;

    // display_path is the path displayed to the user to identify the file.
    std::string display_path;

    // defs is the definition ASTs comprising the source file
    std::vector<AstDef*> defs;

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
    , abs_path(std::move(src_file.abs_path))
    , display_path(std::move(src_file.display_path))
    , llvm_di_file(src_file.llvm_di_file)
    {}
};

#endif
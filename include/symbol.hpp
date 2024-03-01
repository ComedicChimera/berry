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

// SymbolKind enumerates the different kinds of symbols.
enum SymbolKind {
    SYM_VAR,    // Variable
    SYM_FUNC,   // Function
    SYM_TYPE,   // Type Definition
};

#define UNEXPORTED ((size_t)(-1))

// Symbol represents a named symbol in a Berry module.
struct Symbol {
    // parent_id the ID of the symbol's parent module.
    size_t parent_id;

    // name is the name of the symbol.
    std::string_view name;

    // span is the source location of the symbol definition.
    TextSpan span;

    // kind is the kind of the symbol.
    SymbolKind kind;

    // type is the data type of the symbol.
    Type* type { nullptr };

    // immut indicates whether the symbol is immutable.
    bool immut { false };

    // export_num stores the index of the symbol's export table entry if it has
    // one.  If the symbol is unexported, then this will be SYM_UNEXPORTED.
    size_t export_num { UNEXPORTED };

    // llvm_value contains the LLVM value bound to the symbol.
    llvm::Value* llvm_value { nullptr };
};

/* -------------------------------------------------------------------------- */

struct SourceFile;
struct AstDef;

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

    // symbol_table is the module's global symbol table.
    std::unordered_map<std::string_view, Symbol*> symbol_table;

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

        // usages lists all the exports referred to be this dependency.
        std::unordered_set<size_t> usages;

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

    // ExportEntry is an entry of the module's export table.
    struct ExportEntry {
        // def the AST definition that is exported.
        AstDef* def;

        // symbol_num identifies the specific symbol within the definition that
        // is being referenced.  For most definitions, this will be zero.
        int symbol_num;
    };

    // export_table is the table of definitions exported by the module.
    std::vector<ExportEntry> export_table;

    // named_table stores the module's named type dependencies.
    NamedTypeTable named_table;
};

/* -------------------------------------------------------------------------- */

// SourceFile represents a single source file in a Berry module.
struct SourceFile {
    // parent is the module the file is apart of.
    Module* parent;
    
    // abs_path is the absolute path to the file.
    std::string abs_path;

    // display_path is the path displayed to the user to identify the file.
    std::string display_path;

    // defs is the definition ASTs comprising the file.
    std::vector<AstDef*> defs;

    // import_table stores the package's imports.
    std::unordered_map<std::string_view, size_t> import_table;

    // llvm_di_file is the debug info scope associated with this file.
    llvm::DIFile* llvm_di_file { nullptr };

    SourceFile(Module* parent_, std::string&& abs_path_, std::string&& display_path_)
    : parent(parent_)
    , abs_path(std::move(abs_path_))
    , display_path(std::move(display_path_))
    , llvm_di_file(nullptr)
    {}

    SourceFile(SourceFile&& src_file)
    : parent(src_file.parent)
    , abs_path(std::move(src_file.abs_path))
    , display_path(std::move(src_file.display_path))
    , defs(std::move(src_file.defs))
    , llvm_di_file(src_file.llvm_di_file)
    {}
};

#endif
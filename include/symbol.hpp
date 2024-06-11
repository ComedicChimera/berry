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
    SYM_COMPTIME = SYM_TYPE | SYM_CONST // Symbol is compile-time constant
};
typedef int SymbolFlags;

// Symbol represents a named symbol in a Berry module.
struct Symbol {
    // parent_id the ID of the symbol's parent module.
    size_t parent_id;

    // name is the name of the symbol.
    std::string_view name;

    // span is the source location of the symbol declaration.
    TextSpan span;

    // flags are the flags associated with the symbol.
    SymbolFlags flags;

    // decl_num is the number of the global declaration of the symbol. For local
    // symbols, this field is not used.
    size_t decl_num;

    // type is the data type of the symbol.
    Type* type { nullptr };

    // immut indicates whether the symbol is immutable.
    bool immut { false };

    // llvm_value contains the LLVM value bound to the symbol.
    llvm::Value* llvm_value { nullptr };
};
/* -------------------------------------------------------------------------- */

struct SourceFile;
struct AstNode;
struct HirDecl;

// Attribute represents a Berry declaration attribute.
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

using DeclFlags = uint8_t;
enum {
    DECL_EXPORTED = 1,
    DECL_UNSAFE = 2
};

// Decl is a declaration in the module.
struct Decl {
    // file_num is the module-local number of the declaring file.
    size_t file_num;

    // flags is the declaration's associated flags (public, unsafe, etc.)
    DeclFlags flags;

    // attrs contains the declaration's attributes.
    std::span<Attribute> attrs;

    // ast_decl is the declaration AST node.
    AstNode* ast_decl;

    // hir_decl is the declaration HIR node.
    HirDecl* hir_decl { nullptr };

    // color the declarations current graph color (used for cycle detection).
    GColor color;

    Decl(size_t file_number_, DeclFlags flags_, std::span<Attribute> attrs_, AstNode* adecl_)
    : file_num(file_number_)
    , flags(flags_)
    , attrs(attrs_)
    , ast_decl(adecl_)
    {}
};

/* -------------------------------------------------------------------------- */

// The runtime module should always be the second one loaded.
#define BERRY_RT_MOD_ID 1

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

    // decls stores all the module's declarations.  This vector will be sorted
    // into correct initialization order after type checking is done.
    std::vector<Decl*> decls;

    // DepEntry is a module dependency entry.
    struct DepEntry {
        // id is the dependency's ID (index into the dep table).
        size_t id;

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

        DepEntry(size_t id_, std::vector<std::string>&& mod_path_)
        : id(id_)
        , mod(nullptr)
        , mod_path(std::move(mod_path_))
        {}

        DepEntry(size_t id_, Module* mod_)
        : id(id_)
        , mod(mod_)
        , mod_path({ mod_->name })
        {}
    };

    // deps stores the module's dependencies.
    std::vector<DepEntry> deps;

    // MtableNode is a node in the mtable linked list.
    struct MtableNode {
        MethodTable mtable;
        std::unique_ptr<MtableNode> next { nullptr };
    };

    // mtable_list stores all the method tables for named types allocated in the
    // module.  It is arranged as a linked list so as to guarantee the validity
    // of pointers to its elements as the list changes in size.
    std::unique_ptr<MtableNode> mtable_list { nullptr };
};

// SourceFile represents a single source file in a Berry module.
struct SourceFile {
    // parent is the module the file is apart of.
    Module* parent;

    // file_num uniquely identifies the source file within its parent module.
    size_t file_num;
    
    // abs_path is the absolute path to the file.
    std::string abs_path;

    // display_path is the path displayed to the user to identify the file.
    std::string display_path;
    
    // import_table stores the file's named imports.
    std::unordered_map<std::string_view, size_t> import_table;

    // anon_imports stores the file's anonymous imports (`import pkg as _`).
    std::unordered_set<size_t> anon_imports;

    // llvm_di_file is the debug info scope associated with this file.
    llvm::DIFile* llvm_di_file { nullptr };

    SourceFile(Module* parent_, size_t file_number_, std::string&& abs_path_, std::string&& display_path_)
    : parent(parent_)
    , file_num(file_number_)
    , abs_path(std::move(abs_path_))
    , display_path(std::move(display_path_))
    , llvm_di_file(nullptr)
    {}

    SourceFile(SourceFile&& src_file)
    : parent(src_file.parent)
    , file_num(src_file.file_num)
    , abs_path(std::move(src_file.abs_path))
    , display_path(std::move(src_file.display_path))
    , llvm_di_file(src_file.llvm_di_file)
    {}
};

#endif
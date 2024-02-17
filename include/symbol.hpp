#ifndef SYMBOL_H_INC
#define SYMBOL_H_INC

#include <unordered_map>

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
    SYM_VARIABLE,   // Variable
    SYM_FUNC,       // Function
};

// Symbol represents a named symbol in a Berry module.
struct Symbol {
    // name is the name of the symbol.
    std::string_view name;

    // file_id the unique ID of the file defining the symbol/
    int64_t file_id;

    // span is the source location of the symbol definition.
    TextSpan span;

    // kind is the kind of the symbol.
    SymbolKind kind;

    // type is the data type of the symbol.
    Type* type;

    // immut indicates whether the symbol is immutable.
    bool immut;

    // llvm_value contains the LLVM value bound to the symbol.
    llvm::Value* llvm_value;
};

/* -------------------------------------------------------------------------- */

struct SourceFile;

// Module represents a Berry module.
struct Module {
    // id is the module's unique ID.
    int64_t id;

    // name is the identifying name or path of the module (ex: "main", "io.std")
    std::string name;

    // files is the list of files contained in the module.
    std::vector<SourceFile> files;

    // symbol_table is the module's global symbol table.
    std::unordered_map<std::string_view, Symbol*> symbol_table;
};

struct AstDef;

// SourceFile represents a single source file in a Berry module.
struct SourceFile {
    // id the file's unique ID.
    int64_t id;

    // parent is the module the file is apart of.
    Module* parent;
    
    // abs_path is the absolute path to the file.
    std::string abs_path;

    // display_path is the path displayed to the user to identify the file.
    std::string display_path;

    // defs is the definition ASTs comprising the file.
    std::vector<AstDef*> defs;

    // llvm_di_file is the debug info scope associated with this file.
    llvm::DIFile* llvm_di_file { nullptr };
};

#endif
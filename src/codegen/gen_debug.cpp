#include "codegen.hpp"

#define PATH_SEPARATOR "/"

void DebugGenerator::EmitFileInfo(SourceFile& src_file) {
    if (no_emit) {
        return;
    }

    auto last_sep = src_file.abs_path.rfind(PATH_SEPARATOR);
    llvm::DIFile* di_file;
    if (last_sep == std::string::npos) {
        di_file = db.createFile(src_file.abs_path, ".");
    } else {
        auto file_dir = src_file.abs_path.substr(0, last_sep);
        auto file_name = src_file.abs_path.substr(last_sep + 1);

        di_file = db.createFile(file_name, file_dir);
    }

    // TODO: multiple compile units kekw?
    db.createCompileUnit(
        llvm::dwarf::DW_LANG_C, 
        di_file,
        "berryc-v0.0.1",
        false,
        "",
        0
    );

    file_scopes[src_file.id] = di_file;
}

void DebugGenerator::SetCurrentFile(SourceFile& src_file) {
    if (no_emit) {
        return;
    }

    Assert(file_scopes.find(src_file.id) != file_scopes.end(), "file debug scope not created");

    curr_file = file_scopes[src_file.id];
}

void DebugGenerator::FinishModule() {
    db.finalize();
}

/* -------------------------------------------------------------------------- */

void DebugGenerator::EmitFuncProto(AstFuncDef& fd, llvm::Function* ll_func) {
    Assert(curr_file != nullptr, "function debug info missing enclosing file");

    // auto* sub = db.createFunction(
    //     curr_file,
    //     fd.symbol->name,
    //     ll_func->getLinkage() == llvm::GlobalValue::ExternalLinkage ? "external" : "private",
    //     curr_file,
    //     fd.span.start_line
    // );
}
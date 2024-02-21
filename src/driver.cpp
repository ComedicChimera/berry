#include "driver.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>

#include "llvm/IR/Verifier.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/IR/LegacyPassManager.h"

#include "parser.hpp"
#include "checker.hpp"
#include "codegen.hpp"
#include "linker.hpp"

#include "test/ast_print.hpp"

namespace fs = std::filesystem;

#define FNV_OFFSET_BASIS 0xcbf29ce484222325
#define FNV_PRIME 0x100000001b3

static uint64_t fnvHash(const std::string& name) {
    uint64_t h = FNV_OFFSET_BASIS;

    for (auto& c : name) {
        h ^= c;
        h *= FNV_PRIME;
    }

    return h;
}

static std::string absPath(const std::string& rel_path) {
    std::error_code err_code;
    auto abs_path = fs::absolute(rel_path, err_code);
    if (err_code) {
        ReportFatal("computing absolute path: {}", err_code.message());
    }

    return abs_path.string();
}

static std::string relPath(const std::string& base, const std::string& path) {
    std::error_code err_code;
    auto rel_path = fs::relative(path, base, err_code);
    if (err_code) {
        ReportFatal("computing relative path: {}", err_code.message());
    }

    return rel_path.string();
}

/* -------------------------------------------------------------------------- */

#define FULL_LINE "/* ------------------------------------------------------- */\n"

class Compiler {
    Arena arena;

    BuildConfig& cfg;

    std::unordered_map<uint64_t, Module> mods;
    std::vector<std::string> temp_obj_files;

public:
    Compiler(BuildConfig& cfg)
    : cfg(cfg)
    {}

    void Compile() {
        addDefaultImportPaths();        

        loadModule(cfg.input_path);
        check();

        if (cfg.out_fmt == OUTFMT_DUMPAST) {
            dumpAsts();
        }

        emit();

        switch (cfg.out_fmt) {
        case OUTFMT_EXE: case OUTFMT_SHARED: case OUTFMT_STATIC:
            link();
            break;
        }
    }

private:
    void loadModule(const std::string& mod_path) {
        auto abs_path = absPath(mod_path);
        if (!fs::exists(abs_path)) {
            ReportFatal("no file or directory named {}", abs_path);
        }

        uint64_t mod_id = getUniqueModId(abs_path);
        auto mod_name = fs::path(abs_path).filename().string();

        mods.emplace(mod_id, Module{mod_id, std::move(mod_name)});
        auto& mod = mods[mod_id];

        if (fs::is_regular_file(abs_path)) {
            // TODO
        } else if (fs::is_directory(abs_path)) {
            // TODO
        } else {
            ReportFatal("module path must be to a file or directory");
        }

        // Parser p(arena, file, src_file);
        // p.ParseFile();

        // if (ErrorCount() > 0) {
        //     return false;
        // }
    }

    void check() {
        for (auto& pair : mods) {
            auto& mod = pair.second;
            for (auto& src_file : mod.files) {
                Checker c(arena, src_file); 
                for (auto* def : src_file.defs) {
                    c.CheckDef(def);
                }
            }
        }

        if (ErrorCount() > 0) {
            exit(1);
        }
    }

    void emit() {
        initTargets();

        auto* tm = createTargetMachine();

        llvm::LLVMContext ll_ctx;
        std::vector<llvm::Module&> ll_mods;
        for (auto& pair : mods) {
            auto& mod = pair.second;

            auto& ll_mod = ll_mods.emplace_back(llvm::Module(mod.name, ll_ctx));
            ll_mod.setDataLayout(tm->createDataLayout());
            ll_mod.setTargetTriple(tm->getTargetTriple().str());

            CodeGenerator cg(ll_ctx, ll_mod, mod);
            cg.GenerateModule();
        }

        std::error_code err_code;
        if (cfg.out_fmt == OUTFMT_LLVM || cfg.out_fmt == OUTFMT_ASM || cfg.out_fmt == OUTFMT_OBJ) {
            emitToDir(cfg.out_path);
        } else {
            emitToDir(".");
        }
    }


    void link() {
        LinkConfig lconfig { "out.exe", temp_obj_files };
        lconfig.obj_files.insert(lconfig.obj_files.end(), cfg.libs.begin(), cfg.libs.end());

        bool link_result = RunLinker(lconfig);
        for (auto& obj_file : temp_obj_files) {
            RemoveObjFile(obj_file);
        }

        if (!link_result) {
            exit(1);
        }
    }

    /* ---------------------------------------------------------------------- */

    uint64_t getUniqueModId(const std::string& mod_abs_path) {
        uint64_t id = fnvHash(mod_abs_path);

        while (true) {
            auto it = mods.find(id);
            if (it != mods.end()) {
                id++;
            } else {
                break;
            }
        }

        return id;
    }

    void addDefaultImportPaths() {
        auto input_stem = fs::path(cfg.input_path).stem().string();
        cfg.import_paths.emplace(
            cfg.import_paths.begin(), 
            std::move(input_stem)
        );

        // TODO: locate `mods` directory
    }

    /* ---------------------------------------------------------------------- */

    void initTargets() {
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmParser();
        llvm::InitializeNativeTargetAsmPrinter();
    }

    llvm::TargetMachine* createTargetMachine() {
        auto native_target_triple = llvm::sys::getDefaultTargetTriple();

        std::string err_msg;
        auto* target = llvm::TargetRegistry::lookupTarget(native_target_triple, err_msg);
        if (!target) {
            ReportFatal("finding native target: {}", err_msg);
        }

        llvm::TargetOptions target_opt;
        return target->createTargetMachine(
            native_target_triple, 
            "generic", 
            "", 
            target_opt, 
            llvm::Reloc::PIC_
        );
    }

    void emitToDir(const std::string& dir) {
        // llvm::raw_fd_ostream out_file("out.o", err_code, llvm::sys::fs::OF_None);
        // if (err_code) {
        //     fatal("error: opening output file: {}", err_code.message());
        // }

        // llvm::legacy::PassManager pass;
        // if (tm->addPassesToEmitFile(pass, out_file, nullptr, llvm::CodeGenFileType::CGFT_ObjectFile)) {
        //     std::cout << "error: target machine was unable to generate output file\n";
        //     return false;
        // }
        // pass.run(ll_mod);
        // out_file.flush();
        // out_file.close();        
    }

    /* ---------------------------------------------------------------------- */

    void dumpAsts() {
        for (auto& pair : mods) {
            auto& mod = pair.second;
            for (auto& file : mod.files) {
                std::cout << FULL_LINE;
                std::cout << std::format("mod = {}, file = {}:\n\n", mod.name, file.display_path);
                PrintAst(file);
                std::cout << FULL_LINE << '\n';
            }
        }

        exit(0);
    }
};

void BryCompile(BuildConfig& cfg) {
    Compiler c(cfg);
    c.Compile();
}
#include "driver.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>
namespace fs = std::filesystem;

#include "llvm/IR/Verifier.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/IR/LegacyPassManager.h"

#include "loader.hpp"
#include "checker.hpp"
#include "codegen.hpp"
#include "linker.hpp"

#include "test/ast_print.hpp"

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
    const BuildConfig& cfg;

    Arena arena;
    Loader loader;

    std::vector<std::string> obj_files;
    std::string out_dir;
    bool should_delete_out_dir;

public:
    Compiler(const BuildConfig& cfg)
    : cfg(cfg)
    , loader(arena, cfg.import_paths)
    {}

    void Compile() {
        loader.LoadDefaults();
        loader.LoadAll(cfg.input_path);

        check();

        switch (cfg.out_fmt) {
        case OUTFMT_EXE:
        case OUTFMT_STATIC:
        case OUTFMT_SHARED:
            out_dir = ".berry_temp";
            should_delete_out_dir = true;
            prepareOutDir();

            emit();
            link();
            break;
        case OUTFMT_OBJ:
        case OUTFMT_ASM:
        case OUTFMT_LLVM:
            out_dir = cfg.out_path;
            should_delete_out_dir = false;
            prepareOutDir();

            emit();
            break;
        case OUTFMT_DUMPAST:
            for (auto& mod : loader) {
                for (auto& file : mod.files) {
                    std::cout << FULL_LINE;
                    std::cout << std::format("mod = {}, file = {}:\n\n", mod.name, file.display_path);
                    PrintAst(file);
                    std::cout << FULL_LINE << '\n';
                }
            }
            return;
        }

        if (should_delete_out_dir) {
            std::error_code ec;
            fs::remove_all(out_dir, ec);
            if (ec) {
                ReportError("failed to delete temporary files: {}", ec.message());
            }
        }
    }

private:
    void check() {
        for (auto& mod : loader) {
            for (auto& src_file : mod.files) {
                Checker c(arena, src_file); 
                for (auto* def : src_file.defs) {
                    c.CheckDef(def);
                }
            }
        }

        if (ErrorCount() > 0) {
            throw CompileError{};
        }
    }

    void emit() {
        initTargets();

        auto* tm = createTargetMachine();

        llvm::LLVMContext ll_ctx;
        std::vector<llvm::Module&> ll_mods;
        for (auto& mod : loader) {
            auto& ll_mod = ll_mods.emplace_back(llvm::Module(std::format("m{}-{}.ll", mod.id, mod.name), ll_ctx));
            ll_mod.setDataLayout(tm->createDataLayout());
            ll_mod.setTargetTriple(tm->getTargetTriple().str());

            CodeGenerator cg(ll_ctx, ll_mod, mod);
            cg.GenerateModule();
        }

        std::error_code ec;
        if (cfg.out_fmt == OUTFMT_LLVM) {
            for (auto& ll_mod : ll_mods) {
                auto out_path = (fs::path(out_dir) / fs::path(ll_mod.getModuleIdentifier() + ".ll")).string();

                llvm::raw_fd_ostream out_file(out_path, ec, llvm::sys::fs::OF_None);
                if (ec) {
                    ReportFatal("error: opening output file: {}", ec.message());
                }

                ll_mod.print(out_file, nullptr);

                out_file.flush();
                out_file.close();
            }

            return;
        }

        bool is_asm = cfg.out_fmt == OUTFMT_ASM;

        std::string file_ext;
        if (is_asm) {
            file_ext = ".asm";
        } else {
            #if OS_WINDOWS
                file_ext = ".obj";
            #else
                file_ext = ".o";
            #endif
        }

        for (auto& ll_mod : ll_mods) {
            auto out_path = (fs::path(out_dir) / fs::path(ll_mod.getModuleIdentifier() + file_ext)).string();
                
            llvm::raw_fd_ostream out_file(out_path, ec, llvm::sys::fs::OF_None);
            if (ec) {
                ReportFatal("error: opening output file: {}", ec.message());
            }

            llvm::legacy::PassManager pass;
            auto file_type = is_asm ? llvm::CodeGenFileType::CGFT_AssemblyFile : llvm::CodeGenFileType::CGFT_ObjectFile;
            if (tm->addPassesToEmitFile(pass, out_file, nullptr, file_type)) {
                ReportFatal("target machine was unable to generate output file\n");
            }

            pass.run(ll_mod);
            out_file.flush();
            out_file.close();  

            if (!is_asm) {
                obj_files.push_back(out_path);
            }
        }
    }


    void link() {
        auto out_path_fs = fs::path(cfg.out_path);
        if (!out_path_fs.has_extension()) {
            #if OS_WINDOWS
                switch (cfg.out_fmt) {
                case OUTFMT_EXE:
                    out_path_fs += ".exe";
                    break;
                case OUTFMT_STATIC:
                    out_path_fs += ".lib";
                    break;
                case OUTFMT_SHARED:
                    out_path_fs += ".dll";
                    break;
                }
            #else
                switch (cfg.out_fmt) {
                case OUTFMT_STATIC:
                    out_path_fs += ".a";
                    break;
                case OUTFMT_SHARED:
                    out_path_fs += ".so";
                    break;
                }
            #endif
        }

        // TODO: different output formats (lib, dll)
        LinkConfig lconfig { out_path_fs.string(), obj_files };
        lconfig.obj_files.insert(lconfig.obj_files.end(), cfg.libs.begin(), cfg.libs.end()); 

        RunLinker(lconfig);
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

    /* ---------------------------------------------------------------------- */

    void prepareOutDir() {
        std::error_code ec;
        if (fs::exists(out_dir)) {
            fs::remove_all(out_dir, ec);
            if (ec) {
                ReportFatal("failed to remove old output files: {}", ec.message());
            }
        }

        fs::create_directories(out_dir, ec);
        if (ec) {
            ReportFatal("failed to create output directory: {}", ec.message());
        }
    }
};

bool Compile(const BuildConfig& cfg) {
    Compiler c(cfg);

    try {
        c.Compile();
        return ErrorCount() == 0;
    } catch (CompileError&) {
        return false;
    }
}
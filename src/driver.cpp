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
#include "parser.hpp"
#include "checker.hpp"
#include "codegen.hpp"
#include "linker.hpp"

#include "test/ast_print.hpp"

/* -------------------------------------------------------------------------- */

class Compiler {
    const BuildConfig& cfg;

    Arena arena;
    Loader loader;

    std::vector<std::string> obj_files;
    std::string out_dir;
    bool should_delete_out_dir { false };

    llvm::Triple target_triple;

public:
    Compiler(const BuildConfig& cfg)
    : cfg(cfg)
    , loader(arena, cfg.import_paths)
    // TODO: set based on build arguments.
    , target_triple(llvm::sys::getDefaultTargetTriple())
    {
        initPlatform();
    }

    void Compile() {
        loader.LoadAll(cfg.input_path);

        if (ErrorCount() > 0) {
            return;
        }

        check();

        if (ErrorCount() > 0) {
            return;
        }

        switch (cfg.out_fmt) {
        case OUTFMT_EXE:
        case OUTFMT_STATIC:
        case OUTFMT_SHARED:
            out_dir = ".berry-temp";
            should_delete_out_dir = true;
            prepareOutDir();

            emit();
            link();
            break;
        case OUTFMT_OBJ:
        case OUTFMT_ASM:
        case OUTFMT_LLVM:
            out_dir = cfg.out_path;
            prepareOutDir();

            emit();
            break;
        case OUTFMT_DUMPAST:
            for (auto& mod : loader) {
                PrintModuleAst(mod);
            }
            return;
        }
    }

    ~Compiler() {
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
            Checker c(arena, mod); 
            c.CheckModule();
        }

        if (ErrorCount() > 0) {
            throw CompileError{};
        }
    }

    void emit() {
        // Create the platform target machine.
        initTargets();
        auto* tm = createTargetMachine();
                       
        // The modules in this vector have to be unique pointers because it
        // seems like for whatever reason, the LLVM api deletes both the copy
        // and the move constructor of llvm::Module, so it is impossible to
        // store them directly in a vector.  This is my hypothesis anyway; at
        // any rate, any version of this code that stores the LLVM modules as
        // values won't compile (results a slurry of C++ template errors).
        llvm::LLVMContext ll_ctx; 
        std::vector<std::unique_ptr<llvm::Module>> ll_mods;

        // Create the main module.
        auto& main_mod = *ll_mods.emplace_back(std::make_unique<llvm::Module>("_$berry_main", ll_ctx));
        main_mod.setDataLayout(tm->createDataLayout());
        main_mod.setTargetTriple(tm->getTargetTriple().str());

        MainBuilder mainb(ll_ctx, main_mod);

        // Generate all the user modules.
        auto sorted_mods = loader.SortModulesByDepGraph();
        for (auto* mod : sorted_mods) {
            auto& ll_mod = ll_mods.emplace_back(std::make_unique<llvm::Module>(std::format("m{}-{}", mod->id, mod->name), ll_ctx));
            ll_mod->setDataLayout(tm->createDataLayout());
            ll_mod->setTargetTriple(tm->getTargetTriple().str());

            CodeGenerator cg(ll_ctx, *ll_mod, *mod, cfg.should_emit_debug, mainb, arena);
            cg.GenerateModule();
        }

        if (cfg.out_fmt == OUTFMT_EXE) {
            mainb.GenUserMainCall(loader.GetRootModule());
        }

        mainb.FinishMain();

        // Emit the modules to LLVM if that is our output format.
        std::error_code ec;
        if (cfg.out_fmt == OUTFMT_LLVM) {
            for (auto& ll_mod : ll_mods) {
                auto out_path = (fs::path(out_dir) / fs::path(ll_mod->getModuleIdentifier() + ".ll")).string();

                llvm::raw_fd_ostream out_file(out_path, ec, llvm::sys::fs::OF_None);
                if (ec) {
                    ReportFatal("error: opening output file: {}", ec.message());
                }

                ll_mod->print(out_file, nullptr);

                out_file.flush();
                out_file.close();
            }

            return;
        }

        // Otherwise, generate the appropriate output code using the target machine.
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
            auto out_path = (fs::path(out_dir) / fs::path(ll_mod->getModuleIdentifier() + file_ext)).string();
                
            emitModuleToFile(tm, ll_mod, out_path, is_asm);

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
        LinkConfig lconfig { out_path_fs.string(), obj_files, cfg.should_emit_debug };
        lconfig.obj_files.insert(lconfig.obj_files.end(), cfg.libs.begin(), cfg.libs.end()); 

        RunLinker(lconfig);
    }

    /* ---------------------------------------------------------------------- */

    void initPlatform() {
        Parser::platform_meta_vars.os = target_triple.getOSName();
        Parser::platform_meta_vars.debug = cfg.should_emit_debug ? "debug" : "";

        switch (target_triple.getArch()) {
        case llvm::Triple::x86:
            Parser::platform_meta_vars.arch = "i386";
            Parser::platform_meta_vars.arch_size = "32";
            platform_int_type = &prim_i32_type;
            platform_uint_type = &prim_u32_type;
            break;
        case llvm::Triple::x86_64:
            Parser::platform_meta_vars.arch = "amd64";
            Parser::platform_meta_vars.arch_size = "64";
            platform_int_type = &prim_i64_type;
            platform_uint_type = &prim_u64_type;
            break;
        default:
            ReportFatal("unsupported architecture: {}", target_triple.getArchName());
        }
    }

    void initTargets() {
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmParser();
        llvm::InitializeNativeTargetAsmPrinter();
    }

    llvm::TargetMachine* createTargetMachine() {
        std::string err_msg;
        auto* target = llvm::TargetRegistry::lookupTarget(target_triple.str(), err_msg);
        if (!target) {
            ReportFatal("finding native target: {}", err_msg);
        }

        std::string march { "generic" };

        // TODO: handle non-native targets
        march = llvm::sys::getHostCPUName();
        // if (target_triple.getArch() == llvm::Triple::x86_64) {
        //     march = "x86-64-v2";
        // }

        llvm::TargetOptions target_opt;
        return target->createTargetMachine(
            target_triple.str(), 
            march, 
            "", 
            target_opt, 
            llvm::Reloc::PIC_
        );
    }

    void emitModuleToFile(llvm::TargetMachine* tm, std::unique_ptr<llvm::Module>& ll_mod, const std::string& out_path, bool is_asm) {
        std::error_code ec;
        llvm::raw_fd_ostream out_file(out_path, ec, llvm::sys::fs::OF_None);
        if (ec) {
            ReportFatal("error: opening output file: {}", ec.message());
        }

        llvm::legacy::PassManager pass;
        auto file_type = is_asm ? llvm::CodeGenFileType::CGFT_AssemblyFile : llvm::CodeGenFileType::CGFT_ObjectFile;
        if (tm->addPassesToEmitFile(pass, out_file, nullptr, file_type)) {
            ReportFatal("target machine was unable to generate output file\n");
        }

        pass.run(*ll_mod);
        out_file.flush();

        // LLVM wants to do this automatically and throws a random assertion
        // error if you try to do it yourself.  This is why this has to be
        // factored out into its own function: the pass manager and out_file has
        // to go out of scope at the same time, or LLVM throws a fit.
        // out_file.close();  
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
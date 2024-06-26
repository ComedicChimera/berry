#include "driver.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <chrono>

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
#include "target.hpp"

/* -------------------------------------------------------------------------- */

class Compiler {
    const BuildConfig& cfg;

    Arena arena;
    Arena ast_arena;
    Loader loader;

    std::vector<std::string> obj_files;
    std::string out_dir;
    bool should_delete_out_dir { false };

    llvm::TargetMachine* tmach;

    using Clock = std::chrono::steady_clock;
    using Second = std::chrono::duration<double, std::ratio<1>>;

    std::chrono::time_point<Clock> profile_start;
    std::string profile_section;

public:
    Compiler(const BuildConfig& cfg)
    : cfg(cfg)
    , loader(arena, ast_arena, cfg.import_paths)
    {
        initPlatform();
    }

    void Compile() {
        startTimer("Loader");
        loader.LoadAll(cfg.input_path);
        endTimer();

        if (ErrorCount() > 0) {
            return;
        }

        startTimer("Checker");
        check();
        endTimer();

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
        for (auto* mod : loader.SortModulesByDepGraph()) {
            Checker c(arena, *mod); 
            c.CheckModule();
        }

        ast_arena.Release();

        if (ErrorCount() > 0) {
            throw CompileError{};
        }
    }

    void emit() {
        auto& tp = GetTargetPlatform();
                      
        // The modules in this vector have to be unique pointers because it
        // seems like for whatever reason, the LLVM api deletes both the copy
        // and the move constructor of llvm::Module, so it is impossible to
        // store them directly in a vector.  This is my hypothesis anyway; at
        // any rate, any version of this code that stores the LLVM modules as
        // values won't compile (results a slurry of C++ template errors).
        std::vector<std::unique_ptr<llvm::Module>> ll_mods;

        // Create the main module.
        auto& main_mod = *ll_mods.emplace_back(std::make_unique<llvm::Module>("_$berry_main", tp.ll_context));
        main_mod.setDataLayout(tmach->createDataLayout());
        main_mod.setTargetTriple(tmach->getTargetTriple().str());

        startTimer("CodeGen");
        MainBuilder mainb(tp.ll_context, main_mod);

        // Generate all the user modules.
        for (auto* mod : loader.SortModulesByDepGraph()) {
            auto& ll_mod = ll_mods.emplace_back(std::make_unique<llvm::Module>(std::format("m{}-{}", mod->id, mod->name), tp.ll_context));
            ll_mod->setDataLayout(*tp.ll_layout);
            ll_mod->setTargetTriple(tp.ll_triple.str());

            CodeGenerator cg(tp.ll_context, *ll_mod, *mod, cfg.should_emit_debug, mainb, arena);
            cg.GenerateModule();
        }

        if (cfg.out_fmt == OUTFMT_EXE) {
            mainb.GenUserMainCall(loader.GetRootModule());
        }

        mainb.FinishMain();
        endTimer();

        // Emit the modules to LLVM if that is our output format.
        startTimer("LLVM Compile");
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

            endTimer();
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
                
            emitModuleToFile(ll_mod, out_path, is_asm);

            if (!is_asm) {
                obj_files.push_back(out_path);
            }
        }

        endTimer();
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

        startTimer("Linker");
        RunLinker(lconfig);
        endTimer();
    }

    /* ---------------------------------------------------------------------- */

    void initPlatform() {
        auto& tp = GetTargetPlatform();
        tp.debug = cfg.should_emit_debug;
        tp.str_debug = tp.debug ? "true" : "";
        
        auto str_triple = llvm::sys::getDefaultTargetTriple();
        tp.ll_triple = llvm::Triple(str_triple);
        
        tp.os_name = tp.ll_triple.getOSName().str();
        switch (tp.ll_triple.getArch()) {
        case llvm::Triple::x86:
            tp.arch_name = "i386";
            tp.arch_size = 32;
            tp.str_arch_size = "32";

            platform_int_type = &prim_i32_type;
            platform_uint_type = &prim_u32_type;
            break;
        case llvm::Triple::x86_64:
            tp.arch_name = "amd64";
            tp.arch_size = 64;
            tp.str_arch_size = "64";

            platform_int_type = &prim_i64_type;
            platform_uint_type = &prim_u64_type;
            break;
        default:
            ReportFatal("unsupported architecture: {}", tp.ll_triple.getArchName().str());
        }

        initTargets();
        tmach = createTargetMachine(str_triple);
        tp.ll_layout = arena.New<llvm::DataLayout>(tmach->createDataLayout());
    }

    void initTargets() {
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmParser();
        llvm::InitializeNativeTargetAsmPrinter();
    }

    llvm::TargetMachine* createTargetMachine(const std::string& str_triple) {
        std::string err_msg;
        auto* target = llvm::TargetRegistry::lookupTarget(str_triple, err_msg);
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
            str_triple, 
            march, 
            "", 
            target_opt, 
            llvm::Reloc::PIC_
        );
    }

    void emitModuleToFile(std::unique_ptr<llvm::Module>& ll_mod, const std::string& out_path, bool is_asm) {
        std::error_code ec;
        llvm::raw_fd_ostream out_file(out_path, ec, llvm::sys::fs::OF_None);
        if (ec) {
            ReportFatal("error: opening output file: {}", ec.message());
        }

        llvm::legacy::PassManager pass;
        auto file_type = is_asm ? llvm::CodeGenFileType::CGFT_AssemblyFile : llvm::CodeGenFileType::CGFT_ObjectFile;
        if (tmach->addPassesToEmitFile(pass, out_file, nullptr, file_type)) {
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

    void startTimer(const std::string& section) {
        profile_section = section;
        profile_start = Clock::now();
    }

    void endTimer() {
        auto diff = std::chrono::duration_cast<Second>(Clock::now() - profile_start).count();
        std::cout << "[PROFILE] " << profile_section << " ";
        printf("%.2f ms\n", diff * 1000.0f);
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
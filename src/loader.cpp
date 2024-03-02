#include "loader.hpp"

#include <locale>
#include <codecvt>

#include "parser.hpp"

#if OS_WINDOWS
    #define WIN32_LEAN_AND_MEAN 1
	#define WIN32_MEAN_AND_LEAN 1
    #include <Windows.h>
#endif

static fs::path getExePath() {
    #if OS_WINDOWS
        wchar_t* exe_path_buff = (wchar_t*)malloc(MAX_PATH * sizeof(wchar_t));
        DWORD eb_size = MAX_PATH;
        DWORD needed_size = 0;
        while (true) {
            needed_size = GetModuleFileNameW(nullptr, exe_path_buff, eb_size);
            if (needed_size > eb_size) {
                exe_path_buff = (wchar_t*)realloc(exe_path_buff, needed_size * sizeof(wchar_t));
                continue;
            }

            break;
        }

        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
        return fs::canonical(converter.to_bytes(exe_path_buff));
    #else
        return fs::canonical("/proc/self/exe");
    #endif
}

static fs::path findBerryPath() {
    const char* bp_env_var = getenv("BERRY_PATH");

    fs::path berry_path;
    if (bp_env_var == nullptr) {
        auto exe_path = getExePath().parent_path();

        if (exe_path.filename() == "Debug" || exe_path.filename() == "Release") {
            exe_path = exe_path.parent_path();
        }

        if (exe_path.filename() == "bin" || exe_path.filename() == "out" || exe_path.filename() == "build") {
            exe_path = exe_path.parent_path();
        }

        if (fs::exists(exe_path / "mods" / "std")) {
            return exe_path;
        } else {
            berry_path = fs::current_path();
        }
    } else {
        berry_path = bp_env_var;
    }

    if (!fs::exists(berry_path / "mods" / "std")) {
        ReportFatal("unable to locate Berry standard library; consider defining BERRY_PATH environment variable");
    }

    return berry_path;
}

/* -------------------------------------------------------------------------- */

static std::string createDisplayPath(const fs::path& local_path, const fs::path& abs_path) {
    std::error_code ec;
    auto display_path = local_path.filename() / fs::relative(abs_path, local_path, ec);
    if (ec) {
        ReportFatal("computing display path for source file: {}", ec.message());
    }

    return display_path.string();
}

/* -------------------------------------------------------------------------- */

Loader::Loader(Arena& arena, const std::vector<std::string>& import_paths_) 
: arena(arena)
{
    import_paths.reserve(import_paths_.size());
    for (auto& str_path : import_paths_) {
        import_paths.emplace_back(str_path);
    }
}

void Loader::LoadAll(const std::string& root_mod) {
    auto& core_mod = loadDefaults();

    auto root_path = fs::path(root_mod);
    
    if (!fs::exists(root_path)) {
        ReportFatal("no file or directory exists at input path {}", root_mod);
    }

    std::error_code ec;
    root_path = fs::absolute(root_path, ec);
    if (ec) {
        ReportFatal("computing absolute path to root module: {}", ec.message());
    }

    loadRootModule(root_path);

    while (load_queue.size() > 0) {
        auto entry = load_queue.front();
        load_queue.pop();

        auto it = mod_table.find(entry.mod_path.string());
        if (it != mod_table.end()) {
            entry.dep.mod = &it->second;
        } else {
            entry.dep.mod = &loadModule(entry.local_path, entry.mod_path);
        }
    }

    for (auto& mod : *this) {
        if (mod.id != core_mod.id) {
            mod.deps.emplace_back(&core_mod);
        }
    }

    checkForImportCycles();
}

/* -------------------------------------------------------------------------- */

Module& Loader::loadDefaults() {
    auto berry_path = findBerryPath();

    auto std_path = berry_path / "mods" / "std";
    import_paths.emplace_back(std_path);

    auto& core_mod = loadModule(std_path, std_path / "core");
    Assert(core_mod.deps.size() == 0, "core module must have no dependencies");

    loadModule(std_path, std_path / "runtime");
    return core_mod;
}

void Loader::loadRootModule(fs::path& root_mod_abs_path) {
    auto local_path = root_mod_abs_path.parent_path();

    if (fs::is_directory(root_mod_abs_path)) {
        root_mod = &loadModule(local_path, root_mod_abs_path);
    } else if (fs::is_regular_file(root_mod_abs_path)) {
        SourceFile src_file { 
            nullptr,
            root_mod_abs_path.string(), 
            (local_path.filename() / root_mod_abs_path.filename()).string() 
        };

        auto mod_name = getModuleName(src_file);
        if (mod_name == "") {
            return;
        }

        if (mod_name == root_mod_abs_path.filename().replace_extension()) {
            // src_file is its own module.
            auto& mod = addModule(root_mod_abs_path, mod_name);

            src_file.parent = &mod;
            mod.files.emplace_back(std::move(src_file));

            parseModule(mod);
            resolveImports(local_path, mod);

            root_mod = &mod;
        } else {
            // Module is a directory.
            local_path.remove_filename();
            root_mod = &loadModule(local_path, root_mod_abs_path.parent_path());
        }
    } else {
        ReportFatal("input path must be a file or directory");
    }
}

Module& Loader::loadModule(const fs::path& local_path, const fs::path& mod_abs_path) {
    auto& mod = initModule(local_path, mod_abs_path);
    parseModule(mod);
    resolveImports(local_path, mod);
    return mod;
}

/* -------------------------------------------------------------------------- */

Module& Loader::initModule(const fs::path& local_path, const fs::path& mod_abs_path) {
    auto& mod = addModule(mod_abs_path, mod_abs_path.filename().replace_extension().string());

    if (fs::is_directory(mod_abs_path)) {
        for (auto& entry : fs::directory_iterator{mod_abs_path}) {
            if (entry.is_regular_file() && entry.path().extension() == BERRY_FILE_EXT) {
                auto abs_path = mod_abs_path / entry.path();

                SourceFile src_file {
                    &mod,
                    abs_path.string(),
                    createDisplayPath(local_path, abs_path)
                };

                auto mod_name = getModuleName(src_file);
                if (mod_name == "" || mod_name != mod.name) {
                    continue;
                }

                mod.files.emplace_back(std::move(src_file));
            }
        }

        if (mod.files.size() == 0) {
            ReportFatal("module {} contains no source files: located at {}", mod.name, mod_abs_path.string());
        }
    } else if (fs::is_regular_file(mod_abs_path)) {
        mod.files.emplace_back(
            &mod, 
            mod_abs_path.string(), 
            createDisplayPath(local_path, mod_abs_path)
        );
    } else {
        ReportFatal("module must be a file or directory");
    }

    return mod;
}

void Loader::parseModule(Module& mod) {
    for (auto& src_file : mod.files) {
        std::ifstream file(src_file.abs_path);
        if (!file) {
            ReportFatal("opening source file: {}", strerror(errno));
        }

        try {
            Parser p(arena, file, src_file);
            p.ParseFile();
        } catch (CompileError&) {
            // Nothing to do, just stop error bubbling.
        }
    }
}

void Loader::resolveImports(const fs::path& local_path, Module& mod) {
    for (auto& dep : mod.deps) {
        auto maybe_path = findModule(local_path, dep.mod_path);
        if (maybe_path) {
            load_queue.emplace(LoadEntry{local_path, maybe_path.value(), dep});
            continue;
        }

        bool found_match = false;
        for (auto& path : import_paths) {
            maybe_path = findModule(path, dep.mod_path);
            if (maybe_path) {
                found_match = true;
                load_queue.emplace(LoadEntry{path, maybe_path.value(), dep});
                break;
            }
        }

        if (found_match)
            continue;

        std::string fmt_mod_path;
        int n = 0;
        for (auto& path_elem : dep.mod_path) {
            if (n > 0) {
                fmt_mod_path.push_back('.');
            }

            fmt_mod_path.append(path_elem);
            n++;
        }

        for (auto& loc : dep.import_locs) {
            ReportCompileError(loc.src_file.display_path, loc.span, "could not find module {}", fmt_mod_path);
        }
    }
}

std::optional<fs::path> Loader::findModule(const fs::path& search_path, const std::vector<std::string>& mod_path) {
    fs::path path { search_path };
    for (auto& path_elem : mod_path) {
        path.append(path_elem);      
    }

    path.replace_extension(BERRY_FILE_EXT);

    if (fs::exists(path) && fs::is_regular_file(path)) {
        std::ifstream file;
        if (file) {
            SourceFile src_file { nullptr, path.string(), createDisplayPath(search_path, path)};
            Parser p(arena, file, src_file);

            auto mod_name_tok = p.ParseModuleName();
            file.close();
            
            if (mod_name_tok.value == "" || mod_name_tok.value == mod_path.back()) {
                return path;
            }
        }
    }

    path.replace_extension();

    if (fs::exists(path) && fs::is_directory(path)) {
        return path;
    }

    return {};
}

/* -------------------------------------------------------------------------- */

struct ImportCycle {
    std::vector<Module*> nodes;
    Module::Dependency* bad_dep { nullptr };
    bool done { false };
};

static bool findCycle(Module& mod, std::vector<GColor>& colors, ImportCycle& cycle) {
    colors[mod.id] = COLOR_GREY;

    bool found_cycle = false;
    for (auto& dep : mod.deps) {
        auto* dep_mod = dep.mod;
        
        switch (colors[dep_mod->id]) {
        case COLOR_WHITE:
            found_cycle = findCycle(*dep_mod, colors, cycle);
            if (found_cycle) {
                if (!cycle.done) {
                    cycle.nodes.push_back(&mod);
                    if (cycle.nodes.front()->id == mod.id) {
                        cycle.done = true;
                    }
                }

                goto loop_exit;
            }
            break;
        case COLOR_GREY:
            found_cycle = true;

            cycle.bad_dep = &dep;
            cycle.nodes.push_back(dep_mod);
            cycle.nodes.push_back(&mod);
            if (mod.id == dep_mod->id) {
                cycle.done = true;
            }

            goto loop_exit;
        case COLOR_BLACK:
            // Already visited, nothing to do.
            break;
        }
    }

loop_exit:
    colors[mod.id] = COLOR_BLACK;
    return found_cycle;
}

void Loader::checkForImportCycles() {
    std::vector<GColor> colors { mod_table.size(), COLOR_WHITE };

    for (auto& mod : *this) {
        if (colors[mod.id] == COLOR_WHITE) {
            ImportCycle cycle;
            if (findCycle(mod, colors, cycle)) {
                for (auto& loc : cycle.bad_dep->import_locs) {
                    ReportCompileError(
                        loc.src_file.display_path,
                        loc.span,
                        "import of module {} creates cycle",
                        cycle.bad_dep->mod->name
                    );
                }

                std::string fmt_cycle;
                int n = 0;
                while (!cycle.nodes.empty()) {
                    if (n > 0) {
                        fmt_cycle += " -> ";
                    }

                    auto back = cycle.nodes.back();
                    cycle.nodes.pop_back();

                    fmt_cycle += back->name;
                }

                ReportFatal("import cycle detected:\n\t{}", fmt_cycle);
            }
        }
    }
}

/* -------------------------------------------------------------------------- */

Module& Loader::addModule(const fs::path& mod_abs_path, const std::string& mod_name) {
    return mod_table.emplace(mod_abs_path.string(), Module{
        mod_table.size(),
        mod_name,
    }).first->second;
}

std::string Loader::getModuleName(SourceFile& src_file) {
    try {
        std::ifstream file(src_file.abs_path);
        if (!file) {
            ReportFatal("opening file: {}", strerror(errno));
        }

        Parser p(arena, file, src_file);
        auto mod_tok = p.ParseModuleName();
        file.close();

        auto trimmed_fname = fs::path(src_file.abs_path).filename().replace_extension().string();
        if (mod_tok.kind == TOK_EOF) {
            return trimmed_fname;
        } else if (mod_tok.value != trimmed_fname) {
            auto dir_name = fs::path(src_file.abs_path).parent_path().filename().string();

            if (mod_tok.value != dir_name) {
                ReportCompileError(
                    src_file.display_path, 
                    mod_tok.span, 
                    "module name must be the name of the file or enclosing directory"
                );
                return "";
            }
        }

        return mod_tok.value;
    } catch (CompileError& err) {
        return "";
    }
}
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
        auto exe_path = getExePath();
        exe_path.remove_filename();

        if (exe_path.filename() == "Debug" || exe_path.filename() == "Release") {
            exe_path.remove_filename();
        }

        if (exe_path.filename() == "bin" || exe_path.filename() == "out" || exe_path.filename() == "build") {
            exe_path.remove_filename();
        }

        if (fs::exists(berry_path / "mods" / "std")) {
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

Loader::Loader(Arena& arena, const std::vector<std::string>& import_paths_) 
: arena(arena)
{
    import_paths.reserve(import_paths_.size());
    for (auto& str_path : import_paths_) {
        import_paths.emplace_back(str_path);
    }
}

void Loader::LoadDefaults() {
    auto berry_path = findBerryPath();

    auto std_path = (berry_path / "mods" / "std").string();
    import_paths.push_back(std_path);

    // TODO: load runtime and core
}

void Loader::LoadAll(const std::string& root_mod) {
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

    // TODO: load remaining modules
}

/* -------------------------------------------------------------------------- */

void Loader::loadRootModule(fs::path& root_mod_abs_path) {
    local_path = root_mod_abs_path.stem();

    if (fs::is_directory(root_mod_abs_path)) {
        loadModule(local_path, root_mod_abs_path);
    } else if (fs::is_regular_file(root_mod_abs_path)) {
        SourceFile src_file { 
            getUniqueId(), 
            nullptr,
            root_mod_abs_path.string(), 
            root_mod_abs_path.filename().string() 
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
            resolveImports(mod);
        } else {
            // Module is a directory.
            local_path.remove_filename();
            loadModule(local_path, root_mod_abs_path.stem());
        }
    } else {
        ReportFatal("input path must be a file or directory");
    }
}

void Loader::loadModule(const fs::path& import_path, const fs::path& mod_abs_path) {
    auto& mod = initModule(import_path, mod_abs_path);
    parseModule(mod);
    resolveImports(mod);
}

Module& Loader::initModule(const fs::path& import_path, const fs::path& mod_abs_path) {
    auto& mod = addModule(mod_abs_path, mod_abs_path.filename().replace_extension().string());

    if (fs::is_directory(mod_abs_path)) {
        for (auto& entry : fs::directory_iterator{mod_abs_path}) {
            if (entry.is_regular_file() && entry.path().extension() == BERRY_FILE_EXT) {
                auto abs_path = mod_abs_path / entry.path();

                std::error_code ec;
                auto display_path = import_path.filename() / fs::relative(abs_path, import_path, ec);
                if (ec) {
                    ReportFatal("computing display path for source file: {}", ec.message());
                }

                SourceFile src_file {
                    getUniqueId(),
                    &mod,
                    abs_path.string(),
                    display_path.string()
                };

                auto mod_name = getModuleName(src_file);
                if (mod_name == "" || mod_name != mod.name) {
                    continue;
                }

                mod.files.emplace_back(std::move(src_file));
            }
        }
    } else if (fs::is_regular_file(mod_abs_path)) {
        std::error_code ec;
        auto display_path = import_path.filename() / fs::relative(import_path, mod_abs_path, ec);
        if (ec) {
            ReportFatal("computing display path for source file: {}", ec.message());
        }

        mod.files.emplace_back(
            getUniqueId(), 
            &mod, 
            mod_abs_path.string(), 
            display_path.string()
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

void Loader::resolveImports(Module& mod) {
    // TODO
}

/* -------------------------------------------------------------------------- */

Module& Loader::addModule(const fs::path& mod_abs_path, const std::string& mod_name) {
    return mod_table.emplace(mod_abs_path.string(), Module{
        getUniqueId(),
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
            auto dir_name = fs::path(src_file.abs_path).stem().filename().string();

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

uint64_t Loader::getUniqueId() {
    return id_counter++;
}
#include "loader.hpp"

#include <locale>
#include <codecvt>
#include <filesystem>
namespace fs = std::filesystem;

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

void Loader::LoadDefaults() {
    auto berry_path = findBerryPath();

    auto std_path = (berry_path / "mods" / "std").string();
    import_paths.push_back(std_path);

    // TODO: load runtime and core
}

void Loader::LoadAll(const std::string& root_mod) {
    auto root_path = fs::path(root_mod);
    // TODO: check to see whether root_mod is directory module.

    local_path = root_path.stem().string();
    loadModule(local_path, root_mod);
}

/* -------------------------------------------------------------------------- */

void Loader::loadModule(const std::string& import_path, const std::string& mod_path) {
    std::error_code ec;
    auto mod_abs_path_fs = fs::absolute(mod_path, ec);
    if (ec) {
        ReportFatal("computing absolute path to module: {}", ec.message());
    }

    auto mod_abs_path = mod_abs_path_fs.string();
    auto& mod = mod_table.emplace(mod_abs_path, Module{
        getUniqueId(),
        mod_abs_path_fs.filename().replace_extension().string()
    }).first->second;

    if (fs::is_regular_file(mod_abs_path_fs)) {
        try {
            auto display_path = fs::relative(mod_abs_path_fs, import_path, ec);
            if (ec) {
                ReportFatal("computing file display path: {}", ec.message());
            }

            auto& src_file = mod.files.emplace_back(SourceFile{
                getUniqueId(),
                &mod,
                mod_abs_path,
                display_path.string()
            });

            std::ifstream file(mod_abs_path);
            if (!file) {
                ReportFatal("opening source file: {}", strerror(errno));
            }
            Parser p(arena, file, src_file);
            p.ParseFile();
        } catch (CompileError&) {
            // Stop parse error bubbling
        }
    } else if (fs::is_directory(mod_abs_path_fs)) {
        // TODO: directory modules
    } else {
        ReportFatal("module must be a file or directory");
    }

    // TODO: add modules to load queue
}

// #define FNV_OFFSET_BASIS 0xcbf29ce484222325
// #define FNV_PRIME 0x100000001b3

// static uint64_t fnvHash(const std::string& name) {
//     uint64_t h = FNV_OFFSET_BASIS;

//     for (auto& c : name) {
//         h ^= c;
//         h *= FNV_PRIME;
//     }

//     return h;
// }

uint64_t Loader::getUniqueId() {
    return id_counter++;
}
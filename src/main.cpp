#include <iostream>
#include <fstream>
#include <unordered_map>

#include "driver.hpp"

std::string usage_str = 
    "Usage: berry [options] <filename>\n"
    "\n"
    "Flags:\n"
    "    -h, --help      Print usage message and exit\n"
    "    -d, --debug     Generate debug information\n"
    "    -v, --verbose   Print out compilation steps, list modules compiled\n"
    "    -V, --version   Print the compiler version and exit\n"
    "    -q, --quiet     Compile silently, no command line output\n"
    "\n"
    "Arguments:\n"
    "    -o, --outpath   Specify the output path (default = out[.exe])\n"
    "    -E, --emit      Specify the output format\n"
    "                    :: exe (default), static, shared, obj, asm, llvm, dumpast\n"
    "    -g, --gendebug  Specify the debug format, automatically enables debug info\n"
    "                    :: native (default), dwarf, gdb (= dwarf), codeview, msvc (= codeview)\n"
    "    -L, --libpath   Specify additional linker include directories\n"
    "    -l, --lib       Specify additional static libraries, shared libraries, or objects\n"
    "    -W, --warn      Enable specific warnings\n"
    "    -w, --nowarn    Disable specific warnings\n"
    "    -O, --optlevel  Set optimization level (default = 1)\n\n";

template<typename ...Args>
static void usageError(const std::string fmt, Args&&... args) {
    std::cerr << "fatal: " << std::format(fmt, args...) << '\n\n';
    std::cerr << usage_str;

    exit(1);
}

/* -------------------------------------------------------------------------- */

enum OptName {
    OPT_NONE,

    OPT_HELP,
    OPT_DEBUG,
    OPT_VERBOSE,
    OPT_VERSION,
    OPT_QUIET,
    OPT_OUTPATH,
    OPT_EMIT,
    OPT_GENDEBUG,
    OPT_LIBPATH,
    OPT_LIB,
    OPT_WARN,
    OPT_NOWARN,
    OPT_OPTLEVEL,
    OPT_IMPORT,

    OPTIONS_COUNT
};

struct Arg {
    OptName name;
    std::string_view value;
};

bool opt_requires_value[OPTIONS_COUNT] = {
    false,  // OPT_NONE
    false,  // OPT_HELP
    false,  // OPT_DEBUG
    false,  // OPT_VERBOSE
    false,  // OPT_VERSION
    false,  // OPT_QUIET
    true,   // OPT_OUTPATH
    true,   // OPT_EMIT
    true,   // OPT_GENDEBUG
    true,   // OPT_LIBPATH
    true,   // OPT_LIB
    true,   // OPT_WARN
    true,   // OPT_NOWARN
    true,   // OPT_OPTLEVEL
    true,   // OPT_IMPORT
};

std::unordered_map<char, OptName> opt_shortnames {
    { 'h', OPT_HELP },
    { 'd', OPT_DEBUG },
    { 'v', OPT_VERBOSE },
    { 'V', OPT_VERSION },
    { 'q', OPT_QUIET },
    { 'o', OPT_OUTPATH },
    { 'E', OPT_EMIT },
    { 'g', OPT_GENDEBUG },
    { 'L', OPT_LIBPATH },
    { 'l', OPT_LIB },
    { 'W', OPT_WARN },
    { 'w', OPT_NOWARN },
    { 'O', OPT_OPTLEVEL },
    { 'I', OPT_IMPORT }
};

std::unordered_map<std::string_view, OptName> opt_longnames {
    { "help", OPT_HELP },
    { "debug", OPT_DEBUG },
    { "verbose", OPT_VERBOSE },
    { "version", OPT_VERSION },
    { "quiet", OPT_QUIET },
    { "outpath", OPT_OUTPATH },
    { "emit", OPT_EMIT },
    { "gendebug", OPT_GENDEBUG },
    { "libpath", OPT_LIBPATH },
    { "lib", OPT_LIB },
    { "warn", OPT_WARN },
    { "nowarn", OPT_NOWARN },
    { "optlevel", OPT_OPTLEVEL },
    { "import", OPT_IMPORT }
};

static bool getArg(Arg& data, int& argc, char**& argv) {
    data.name = OPT_NONE;

    bool expect_value = false;
    std::string_view saved_opt_name { "" };
    while (argc > 0) {
        std::string_view arg = argv[0];
        argv = argv + 1;
        argc--;

        if (arg.size() == 0) {
            continue;
        } else if (arg[0] == '-' && arg.size() > 1) {
            if (expect_value) {
                usageError("{} requires a value", saved_opt_name);
            } else if (arg[1] == '-' && arg.size() > 2) {
                std::string_view opt_name_str = arg.substr(2);
                
                auto it = opt_longnames.find(opt_name_str);
                if (it == opt_longnames.end()) {
                    usageError("unknown option: --{}", opt_name_str);
                }

                data.name = it->second;
            } else if (arg[1] != '-') {
                char opt_name_ch = arg[1];

                auto it = opt_shortnames.find(opt_name_ch);
                if (it == opt_shortnames.end()) {
                    usageError("unknown option: -{}", opt_name_ch);
                }

                data.name = it->second;
                if (arg.size() > 2) {
                    data.value = arg.substr(2);
                    return true;
                }
            }

            expect_value = opt_requires_value[data.name];
            if (!expect_value) {
                return true;
            } else {
                saved_opt_name = arg;
                continue;
            }
        } 

        data.value = arg;
        return true;
    }

    if (expect_value) {
        usageError("{} requires a value", saved_opt_name);
    }

    return false;
}

/* -------------------------------------------------------------------------- */

std::unordered_map<std::string_view, OutputFormat> out_fmt_names {
    { "exe", OUTFMT_EXE },
    { "static", OUTFMT_STATIC },
    { "shared", OUTFMT_SHARED },
    { "obj", OUTFMT_OBJ },
    { "asm", OUTFMT_ASM },
    { "llvm", OUTFMT_LLVM },
    { "dumpast", OUTFMT_DUMPAST },
};

std::unordered_map<std::string_view, DebugInfoFormat> dbg_fmt_names {
    { "native", DBGI_NATIVE },
    { "dwarf", DBGI_DWARF },
    { "gdb", DBGI_DWARF },
    { "codeview", DBGI_CODEVIEW },
    { "msvc", DBGI_CODEVIEW }
};

static void parseArgs(BuildConfig& cfg, int argc, char* argv[]) {
    Arg arg;
    while (getArg(arg, argc, argv)) {
        switch (arg.name) {
        case OPT_NONE: {
            if (cfg.input_path.size() > 0) {
                usageError("multiple input paths specified");
            }

            cfg.input_path = arg.value;
        } break;
        case OPT_HELP:
            std::cout << usage_str;
            exit(0);
            break;
        case OPT_DEBUG:
            cfg.should_emit_debug = true;
            break;
        case OPT_VERBOSE:
            // TODO
            break;
        case OPT_VERSION:
            std::cout << BERRYC_VERSION << "\n\n";
            exit(0);
            break;
        case OPT_QUIET:
            // TODO
            break;
        case OPT_OUTPATH:
            cfg.out_path = arg.value;
            break;
        case OPT_EMIT: {
            auto it = out_fmt_names.find(arg.value);
            if (it == out_fmt_names.end()) {
                usageError("unknown output format");
            }

            cfg.out_fmt = it->second;
        } break;
        case OPT_GENDEBUG: {
            auto it = dbg_fmt_names.find(arg.value);
            if (it == dbg_fmt_names.end()) {
                usageError("unknown debug format");
            }

            cfg.debug_fmt = it->second;
        } break;
        case OPT_LIBPATH:
            cfg.lib_paths.emplace_back(arg.value);
            break;
        case OPT_LIB:
            cfg.libs.emplace_back(arg.value);
            break;
        case OPT_WARN:
            // TODO
            break;
        case OPT_NOWARN:
            // TODO
            break;
        case OPT_OPTLEVEL: {
            try {
                int opt_level = std::stoi(std::string(arg.value));
                if (0 <= opt_level && opt_level < 4) {
                    cfg.opt_level = opt_level;
                } else {
                    throw std::out_of_range{""};
                }
            } catch (std::invalid_argument& ex_ia) {
                usageError("could not convert optlevel to string: {}", ex_ia.what());
            } catch (std::out_of_range& ex_oor) {
                usageError("optlevel must be between 0 and 3");
            }
        } break;
        case OPT_IMPORT:
            cfg.import_paths.emplace_back(arg.value);
            break;
        }
    }

    if (cfg.input_path.size() == 0) {
        usageError("missing input path");
    }
}

int main(int argc, char* argv[]) {
    BuildConfig cfg;

    parseArgs(cfg, argc, argv);

    BryCompile(cfg);
}

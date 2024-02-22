#ifndef DRIVER_H_INC
#define DRIVER_H_INC

#include "base.hpp"

#define BERRYC_VERSION "berryc-dev-0.4.0"

enum OutputFormat {
    OUTFMT_EXE,
    OUTFMT_STATIC,
    OUTFMT_SHARED,
    OUTFMT_OBJ,
    OUTFMT_ASM,
    OUTFMT_LLVM,
    OUTFMT_DUMPAST,

    OUTFMT_DEFAULT,
    OUTFMTS_COUNT
};

enum DebugInfoFormat {
    DBGI_NATIVE,
    DBGI_DWARF,
    DBGI_CODEVIEW,

    DBGIS_COUNT
};

struct BuildConfig {
    std::string input_path;
    std::vector<std::string> import_paths;

    std::string out_path;
    OutputFormat out_fmt;

    bool should_emit_debug;
    DebugInfoFormat debug_fmt;

    std::vector<std::string> libs;
    std::vector<std::string> lib_paths;

    int opt_level;

    BuildConfig()
    : out_path("out")
    , out_fmt(OUTFMT_DEFAULT)
    , should_emit_debug(false)
    , debug_fmt(DBGI_NATIVE)
    , opt_level(1)
    {}
};

bool Compile(const BuildConfig& cfg);

#endif
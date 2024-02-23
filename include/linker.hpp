#ifndef LINKER_H_INC
#define LINKER_H_INC

#include "base.hpp"

struct LinkConfig {
    std::string out_path;
    std::vector<std::string> obj_files;
    bool should_emit_debug { false };
};

bool RunLinker(LinkConfig& cfg);

#endif
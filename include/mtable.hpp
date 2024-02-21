#ifndef MTABLE_H_INC
#define MTABLE_H_INC

#include "symbol.hpp"

class ModuleTable {

public:
    ModuleTable(std::vector<std::string>&& import_paths);
    Module& GetModule(const std::string& path);

    // TODO: modules iterator
};

#endif
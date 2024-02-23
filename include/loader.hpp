#ifndef LOADER_H_INC
#define LOADER_H_INC

#include <filesystem>
namespace fs = std::filesystem;

#include "symbol.hpp"    

#define BERRY_FILE_EXT ".bry"

class Loader {
    using ModuleTable = std::unordered_map<std::string, Module>;

    Arena& arena;
    ModuleTable mod_table;
    std::vector<fs::path> import_paths;
    fs::path local_path;

    uint64_t id_counter { 0 };

public:
    class ModuleIterator {
        ModuleTable::iterator it;

        ModuleIterator(ModuleTable::iterator&& it) : it(std::move(it)) {}

        friend class Loader;

    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = Module;
        using difference_type = std::ptrdiff_t;
        using pointer = Module*;
        using reference = Module&;

        reference operator*() const { return it->second; }
        pointer operator->() { return &it->second; }

        ModuleIterator& operator++() { ++it; return *this; }
        ModuleIterator operator++(int) { ModuleIterator tmp = *this; ++(*this); return tmp; }

        friend bool operator==(const ModuleIterator& a, const ModuleIterator& b) { return a.it == b.it; }
        friend bool operator!=(const ModuleIterator& a, const ModuleIterator& b) { return a.it != b.it; }
    };

    Loader(Arena& arena, const std::vector<std::string>& import_paths);
    void LoadDefaults();
    void LoadAll(const std::string& root_mod);

    inline ModuleIterator begin() { return ModuleIterator(mod_table.begin()); }
    inline ModuleIterator end() { return ModuleIterator(mod_table.end()); }

private:
    void loadRootModule(fs::path& root_mod_abs_path);
    void loadModule(const fs::path& import_path, const fs::path& mod_abs_path);
    Module& initModule(const fs::path& import_path, const fs::path& mod_abs_path);
    void parseModule(Module& mod);
    void resolveImports(Module& mod);

    /* ---------------------------------------------------------------------- */

    Module& addModule(const fs::path& mod_abs_path, const std::string &mod_name);
    std::string getModuleName(SourceFile &src_file);
    uint64_t getUniqueId();
};

#endif
#ifndef LOADER_H_INC
#define LOADER_H_INC

#include <queue>
#include <optional>
#include <filesystem>
namespace fs = std::filesystem;

#include "symbol.hpp"    

#define BERRY_FILE_EXT ".bry"

class Loader {
    using ModuleTable = std::unordered_map<std::string, Module>;

    Arena& global_arena;
    Arena& ast_arena;
    ModuleTable mod_table;
    std::vector<fs::path> import_paths;

    Module* root_mod { nullptr };
    Module* runtime_mod { nullptr };

    struct LoadEntry {
        fs::path local_path;
        fs::path mod_path;

        Module::DepEntry& dep;
    };
    std::queue<LoadEntry> load_queue;

    std::vector<Module*> sorted_mods;

public:
    Loader(Arena& global_arena, Arena& ast_arena, const std::vector<std::string>& import_paths);
    void LoadAll(const std::string& root_mod);

    std::vector<Module*>& SortModulesByDepGraph();
    inline Module& GetRootModule() { return *root_mod; }

    /* ---------------------------------------------------------------------- */

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

    inline ModuleIterator begin() { return ModuleIterator(mod_table.begin()); }
    inline ModuleIterator end() { return ModuleIterator(mod_table.end()); }

private:
    Module& loadDefaults();
    void loadRootModule(fs::path& root_mod_abs_path);
    Module& loadModule(const fs::path& local_path, const fs::path& mod_abs_path);

    /* ---------------------------------------------------------------------- */

    Module& initModule(const fs::path& local_path, const fs::path& mod_abs_path);
    void parseModule(Module& mod);
    void resolveImports(const fs::path& local_path, Module& mod);
    std::optional<fs::path> findModule(const fs::path& search_path, const std::vector<std::string>& mod_path);
    void checkForImportCycles();

    /* ---------------------------------------------------------------------- */

    Module& addModule(const fs::path& mod_abs_path, const std::string &mod_name);
    std::string getModuleName(SourceFile &src_file);

    /* ---------------------------------------------------------------------- */

    void sortModule(Module *mod, std::vector<bool>& visited);
};

#endif
#ifndef LOADER_H_INC
#define LOADER_H_INC

#include "symbol.hpp"    

class Loader {
    using ModuleTable = std::unordered_map<std::string_view, Module>;

    Arena& arena;
    ModuleTable mod_table;
    std::vector<std::string> import_paths;
    std::string local_path;

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

    Loader(Arena& arena, const std::vector<std::string>& import_paths) : arena(arena), import_paths(import_paths) {}
    void LoadDefaults();
    void LoadAll(const std::string& root_mod);

    inline ModuleIterator begin() { return ModuleIterator(mod_table.begin()); }
    inline ModuleIterator end() { return ModuleIterator(mod_table.end()); }
};



#endif
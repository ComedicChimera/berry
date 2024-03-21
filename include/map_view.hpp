#ifndef MAP_VIEW_H
#define MAP_VIEW_H

// WARNING: This file contains both the definition and the implementation of map
// view because C++ templates are painful and irritating.  If you don't need the
// implementation, don't include this file, just define an opaque type.

#include "arena.hpp"

template<typename T>

class MapView {
    struct MapBucket {
        std::string_view key;
        T value;
        MapBucket* next;
    };

    std::span<MapBucket*> table;
    size_t n_pairs;

public:
    inline size_t size() const { return n_pairs; }

    bool contains(std::string_view key);
    T& get(std::string_view key);
    inline T& operator[](std::string_view key) { return get(key); }

    class MapIterator {
        size_t ndx;
        MapBucket* bucket;

        friend class MapView;

    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = std::pair<std::string_view, T>;
        using difference_type = std::ptrdiff_t;
        using pointer = value_type*;
        using reference = value_type&;

        MapIterator(const MapView& view, size_t start_ndx);

        reference operator*();
        pointer operator->();

        MapIterator& operator++();
        MapIterator operator++(int);

        friend bool operator==(const MapIterator& a, const MapIterator& b);
        friend bool operator!=(const MapIterator& a, const MapIterator& b);
    };

    MapIterator begin() { return MapIterator(*this, 0); }
    MapIterator end() { return MapIterator(*this, table.size()); }

private:
    friend class Arena;
    MapView(Arena& arena, std::unordered_map<std::string_view, T>&& map);

    MapBucket* lookup(std::string_view key);
};

#endif
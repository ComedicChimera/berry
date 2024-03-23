#ifndef MAP_VIEW_H
#define MAP_VIEW_H

#include <optional>

#include "arena.hpp"

template<typename T>
class MapView {
    struct MapBucket {
        std::string_view key;
        T value;
        MapBucket* next;

        MapBucket(std::string_view key, T&& value)
        : key(key)
        , value(std::move(value))
        , next(nullptr)
        {}
    };

    std::span<MapBucket*> table;
    size_t n_pairs;

    static std::hash<std::string_view> hasher {};

public:
    MapView(Arena& arena, std::unordered_map<std::string_view, T>&& map) 
    : n_pairs(map.size())
    {
        auto n_buckets = map.bucket_count();
        auto* table_ptr = (MapBucket**)arena.Alloc(n_buckets * sizeof(MapBucket*));
        memset(table_ptr, 0, sizeof(MapBucket*) * n_buckets);

        table = std::span<MapBucket*>(table_ptr, n_buckets);

        for (auto& pair : map) {
            auto hndx = hasher(pair.first) % table.size();

            auto* bucket = table[hndx];
            if (bucket == nullptr) {
                table[hndx] = arena.New<MapBucket>(pair.first, std::move(pair.second));
                continue;
            }

            while (bucket->next != nullptr) {
                bucket = bucket->next;
            }

            bucket->next = arena.New<MapBucket>(pair.first, pair.second);
        }

        map.clear();
    }

    inline size_t size() const { return n_pairs; }
    inline bool contains(std::string_view key) { return lookup(key) != nullptr; }
    inline T& operator[](std::string_view key) { return get(key); }

    T& get(std::string_view key) {
        auto* bucket = lookup(key);

        if (bucket == nullptr)
            Panic("map view has no key named {}", key);

        return bucket->value;
    }

    std::optional<T> try_get(std::string_view key) {
        auto* bucket = lookup(key);

        if (bucket == nullptr)
            return {};

        return bucket->value;
    }

    class MapIterator {
        MapView& view;
        size_t ndx;
        MapBucket* bucket;

        friend class MapView;

    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = value_type*;
        using reference = value_type&;

        MapIterator(MapView& view, size_t start_ndx)
        : view(view)
        , ndx(start_ndx)
        , bucket(nullptr)
        {
            while (ndx < view.table.size() && bucket == nullptr) {
                bucket = view.table[++ndx];
            }
        }

        reference operator*() { return bucket->value; }
        pointer operator->() { return &bucket->value; }

        MapIterator& operator++() {
            if (bucket == nullptr)
                return *this;

            bucket = bucket->next;
            while (ndx < view.table.size() && bucket == nullptr) {
                bucket = view.table[++ndx];
            }
        }

        MapIterator operator++(int) { auto tmp = *this; ++(*this); return tmp; }

        friend bool operator==(const MapIterator& a, const MapIterator& b) { return a.bucket == b.bucket; }
        friend bool operator!=(const MapIterator& a, const MapIterator& b) { return a.bucket != b.bucket; }
    };

    MapIterator begin() { return MapIterator(*this, 0); }
    MapIterator end() { return MapIterator(*this, table.size()); }

private:
    MapBucket* lookup(std::string_view key) {
        auto hndx = hasher(key) % table.size();

        auto* bucket = table[hndx];
        while (bucket != nullptr) {
            if (bucket->key == key)
                return bucket;

            bucket = bucket->next;
        }

        return bucket;
    }
};

#endif
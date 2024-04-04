#ifndef ARENA_H_INC
#define ARENA_H_INC

#include "base.hpp"

// ArenaChunk represents a single chunk of contiguous memory used by the arena.
// The chunks double as a singly linked list. 
struct ArenaChunk {
    // n_used is the number of bytes actually in use.  Coincidentally, it
    // points to the next free address in the chunk's memory buffer.
    size_t n_used;

    // n_alloc is the number of bytes allocated and ready for use.
    size_t n_alloc;

    // prev is the previous chunk in the linked list.
    ArenaChunk* prev;

    // data is the "named pointer" to the memory buffer: the buffer starts after
    // the address of this field.  MUST BE AT THE END OF ArenaChunk.
    byte data[0];
};

// Arena represents a "simple" linear memory chunk in which items are allocated
// in sequence and never individually freed or resized after their creation has
// completed.  Instead, once the objects in the arena are no longer needed, the
// entire arena is disposed of at once.  This is useful for large pools objects
// which have no clear owner and share the same extended lifetime such as types
// or symbols.  
// See: https://www.rfleury.com/p/untangling-lifetimes-the-arena-allocator
class Arena {
    // curr_chunk points the "head" of the linked list of chunks.  Note that
    // this list is stored in reverse order.
    ArenaChunk* curr_chunk;

    // System memory bindings.
    void* sysAlloc(void* start, size_t size);
    bool sysFree(void* block);

public:
    // Creates a new empty arena.
    Arena() : curr_chunk(nullptr) {}

    // Deletes the arena and frees all associated resources.
    ~Arena();

    // Move one arena into another.
    Arena(Arena&&);

    // Arenas cannot be copied.
    Arena(const Arena&) = delete;

    /* ---------------------------------------------------------------------- */

    // Alloc allocates size bytes in the arena.  The bytes will always be
    // contiguous. If the allocation cannot be performed, NULL is returned.
    void* Alloc(size_t size);

    // Reset resets the arena allocation pointer back to the start of the arena
    // and releases all but the first chunk.
    void Reset();

    // Release releases all the memory associated with the arena back to the OS.
    void Release();

    /* ---------------------------------------------------------------------- */

    // New makes a new object of type T in the arena's storage passing args
    // to the constructor of T.
    template<class T, class... Args>
    inline T* New(Args&&... args) {
        T* obj = (T*)Alloc(sizeof(T));
        return std::construct_at(obj, args...);
    }

    // MoveStr moves the contents of str into the arena and returns a string
    // view (slice) to the newly allocated memory.  This deletes the memory
    // associated with str but does perform a copy to move the memory from str
    // into the arena.
    inline std::string_view MoveStr(std::string&& str) {
        if (str.size() == 0) {
            return {};
        }

        size_t len = str.size();
        size_t size = len * sizeof(char);

        char* data = (char*)Alloc(size);
        memcpy_s(data, size, str.data(), size);

        str.clear();

        return { data, len };
    }

    // MoveVec moves the elements of vec into the arena and returns a span
    // (slice) to the newly allocated memory.  This deletes the memory
    // associated with vec but does perform a copy to move the memory from vec
    // into the arena.
    template<class T>
    inline std::span<T> MoveVec(std::vector<T>&& vec) {
        if (vec.size() == 0) {
            return {};
        }

        size_t len = vec.size();
        size_t size = len * sizeof(T);

        T* data = (T*)Alloc(size);
        for (int i = 0; i < len; i++) {
            *(data + i) = std::move(vec[i]);
        }

        vec.clear();

        return { data, len };
    }

private:
    // computeFwdOffset computes the amount to offset the start of a new block
    // of memory so that user memory starts at a properly aligned address.
    size_t computeFwdOffset();

    // alignSize aligns the size so that it is a multiple of the arena alignment.
    size_t alignSize(size_t size);
};

#endif
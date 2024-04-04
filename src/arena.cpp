#include "arena.hpp"

#define ARENA_CHUNK_SIZE ((8 * 1024 * 1024))

#if ARCH_64_BIT
    #define ARENA_ALIGN 8
#else
    #define ARENA_ALIGN 4
#endif

#define max(a, b) ((a) < (b) ? b : a)

Arena::~Arena() {
    Release();
}

Arena::Arena(Arena&& arena) {
    curr_chunk = arena.curr_chunk;
    arena.curr_chunk = nullptr;
}

/* -------------------------------------------------------------------------- */

void* Arena::Alloc(size_t size) {
    if (size == 0) {
        return nullptr;
    }

    size_t fwd_offset = 0;
    size_t full_size = size;
    if (curr_chunk != nullptr) {
        fwd_offset = computeFwdOffset();
        full_size += fwd_offset;
    }

    if (curr_chunk == nullptr || (curr_chunk->n_used + full_size > curr_chunk->n_alloc)) {
        full_size = alignSize(size);
        fwd_offset = 0;

        size_t chunk_size = max(full_size + sizeof(ArenaChunk), ARENA_CHUNK_SIZE);

        ArenaChunk* new_chunk = (ArenaChunk*)sysAlloc(nullptr, chunk_size);
        if (new_chunk == nullptr) {
            return nullptr;
        }

        new_chunk->n_alloc = chunk_size - sizeof(ArenaChunk);
        new_chunk->prev = curr_chunk;

        curr_chunk = new_chunk;
    }

    byte* ptr = curr_chunk->data + curr_chunk->n_used;
    curr_chunk->n_used += full_size;

    return ptr + fwd_offset;
}

size_t Arena::computeFwdOffset() {
    size_t iptr = (size_t)(curr_chunk->data + curr_chunk->n_used);
    
    if (iptr % ARENA_ALIGN > 0) {
        return ARENA_ALIGN - iptr % ARENA_ALIGN;
    }

    return 0;
}

size_t Arena::alignSize(size_t size) {
    if (size % ARENA_ALIGN == 0)
        return size;

    return size + (ARENA_ALIGN - size % ARENA_ALIGN);
}

/* -------------------------------------------------------------------------- */

void Arena::Release() {
    ArenaChunk* prev_chunk;
    while (curr_chunk != nullptr) {
        prev_chunk = curr_chunk->prev;
        sysFree(curr_chunk);
        curr_chunk = prev_chunk;
    }
}

void Arena::Reset() {
    if (curr_chunk == nullptr)
        return;

    auto* prev_chunk = curr_chunk->prev;
    while (prev_chunk != nullptr) {
        sysFree(curr_chunk);
        curr_chunk = prev_chunk;
        prev_chunk = curr_chunk->prev;
    }

    curr_chunk->n_used = 0;
}

/* -------------------------------------------------------------------------- */

#if OS_WINDOWS
    #define WIN32_LEAN_AND_MEAN 1
	#define WIN32_MEAN_AND_LEAN 1
    #include <Windows.h>

    void* Arena::sysAlloc(void* start, size_t size) {
        return VirtualAlloc(start, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    }

    bool Arena::sysFree(void* block) {
        return VirtualFree(block, 0, MEM_RELEASE) > 0;
    }
#else
    #error non-windows arena allocator not yet implemented
#endif
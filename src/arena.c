// arena.c - Cross-platform arena allocator for parser frontend
// Uses mmap (POSIX) and VirtualAlloc (Windows) for large memory regions

#include "jcc.h"
#include "internal.h"
#include <string.h>

// Platform-specific includes and definitions
#if defined(_WIN32) || defined(_WIN64)
#define ARENA_MMAP(size) VirtualAlloc(NULL, (size), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)
#define ARENA_MUNMAP(ptr, size) VirtualFree((ptr), 0, MEM_RELEASE)
#define MAP_FAILED NULL
#else
#define ARENA_MMAP(size) mmap(NULL, (size), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)
#define ARENA_MUNMAP(ptr, size) munmap((ptr), (size))
#endif

// Default block size: 1MB
#define DEFAULT_ARENA_BLOCK_SIZE (1024 * 1024)

// Initialize an arena with a default block size
void arena_init(Arena *arena, size_t default_block_size) {
    if (default_block_size == 0) {
        default_block_size = DEFAULT_ARENA_BLOCK_SIZE;
    }

    arena->default_block_size = default_block_size;
    arena->current = NULL;
    arena->blocks = NULL;
}

// Allocate a new memory block via mmap/VirtualAlloc
static ArenaBlock *arena_new_block(Arena *arena, size_t min_size) {
    // Determine block size (at least default size, but grow if needed)
    size_t size = arena->default_block_size;
    if (min_size > size) {
        size = min_size;
    }

    // Allocate the ArenaBlock structure itself using malloc
    ArenaBlock *block = malloc(sizeof(ArenaBlock));
    if (!block) {
        error("out of memory: failed to allocate ArenaBlock structure");
    }

    // Allocate the actual memory block using mmap/VirtualAlloc
    void *memory = ARENA_MMAP(size);
    if (memory == MAP_FAILED || memory == NULL) {
        free(block);
        error("out of memory: failed to allocate arena block of size %zu", size);
    }

    // Initialize block metadata
    block->base = (char *)memory;
    block->ptr = block->base;
    block->size = size;
    block->next = arena->blocks;

    // Add to block list
    arena->blocks = block;

    return block;
}

// Allocate memory from the arena (bump pointer allocation)
void *arena_alloc(Arena *arena, size_t size) {
    // Align to 16 bytes for proper alignment (sufficient for long double / SIMD)
    size = (size + 15) & ~15;

    // Check if current block has enough space
    if (!arena->current ||
        (arena->current->ptr + size > arena->current->base + arena->current->size)) {
        // After arena_reset all blocks have been rewound; scan the list for one
        // with room before allocating a fresh block. This ensures that reset
        // blocks are actually reused rather than orphaned. (#53)
        ArenaBlock *b = arena->blocks;
        for (; b; b = b->next) {
            if (b->ptr + size <= b->base + b->size)
                break;
        }
        arena->current = b ? b : arena_new_block(arena, size);
    }

    // Allocate from current block (bump pointer)
    void *ptr = arena->current->ptr;
    arena->current->ptr += size;

    return ptr;
}

// Reset arena (reuse all blocks without deallocating).
// Rewinds every block's bump pointer to base so subsequent arena_alloc calls
// can refill them. arena_alloc scans the block list for room, so all blocks
// are eligible for reuse after a reset. (#53)
void arena_reset(Arena *arena) {
    for (ArenaBlock *block = arena->blocks; block; block = block->next) {
        block->ptr = block->base;
    }
    // Point current at the oldest (smallest-index) block so the common case
    // of filling blocks in order starts at the beginning of the list again.
    ArenaBlock *tail = arena->blocks;
    while (tail && tail->next)
        tail = tail->next;
    arena->current = tail;
}

// Destroy arena and free all memory
void arena_destroy(Arena *arena) {
    ArenaBlock *block = arena->blocks;

    while (block) {
        ArenaBlock *next = block->next;

        // Unmap the memory block
        ARENA_MUNMAP(block->base, block->size);

        // Free the ArenaBlock structure
        free(block);

        block = next;
    }

    // Clear arena state
    arena->blocks = NULL;
    arena->current = NULL;
}

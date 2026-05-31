// vm_mem.c - Cross-platform virtual memory reserve/commit for VM segments
//
// Strategy: each segment is reserved as a large PROT_NONE / PAGE_NOACCESS
// range so its base address never moves, then pages are committed on demand
// as the segment grows. This preserves all absolute pointers (LDA3, LEA3,
// MALC) that have already escaped into type-erased storage, and keeps
// heap-range checks valid.

#include "jcc.h"
#include "internal.h"

// Platform shims
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>

static size_t jcc_vm_page_size(void) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (size_t)si.dwPageSize;
}

void *jcc_vm_reserve(size_t bytes) {
    void *p = VirtualAlloc(NULL, bytes, MEM_RESERVE, PAGE_NOACCESS);
    return p; // NULL on failure
}

int jcc_vm_commit(void *base, size_t off, size_t len) {
    size_t pgsz = jcc_vm_page_size();
    // Align offset down, extend length to cover full pages
    size_t aligned_off = off & ~(pgsz - 1);
    size_t aligned_end = (off + len + pgsz - 1) & ~(pgsz - 1);
    size_t aligned_len = aligned_end - aligned_off;
    void *p = VirtualAlloc((char *)base + aligned_off, aligned_len,
                           MEM_COMMIT, PAGE_READWRITE);
    return (p == NULL) ? -1 : 0;
}

void jcc_vm_release(void *base, size_t bytes) {
    (void)bytes;
    VirtualFree(base, 0, MEM_RELEASE);
}

#else // POSIX
#include <sys/mman.h>
#include <unistd.h>

static size_t jcc_vm_page_size(void) {
    return (size_t)sysconf(_SC_PAGESIZE);
}

void *jcc_vm_reserve(size_t bytes) {
    void *p = mmap(NULL, bytes, PROT_NONE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return (p == MAP_FAILED) ? NULL : p;
}

int jcc_vm_commit(void *base, size_t off, size_t len) {
    size_t pgsz = jcc_vm_page_size();
    // Align offset down, extend length to cover full pages
    size_t aligned_off = off & ~(pgsz - 1);
    size_t aligned_end = (off + len + pgsz - 1) & ~(pgsz - 1);
    size_t aligned_len = aligned_end - aligned_off;
    return mprotect((char *)base + aligned_off, aligned_len,
                    PROT_READ | PROT_WRITE);
}

void jcc_vm_release(void *base, size_t bytes) {
    munmap(base, bytes);
}

#endif

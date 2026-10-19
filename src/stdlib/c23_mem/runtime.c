// C23 memory functions absent from macOS and from glibc before 2.43:
// memset_explicit, memalignment, free_sized and free_aligned_sized. Compiled
// into the VM by src/stdlib/stdlib.c and emitted verbatim as -c=native/-m shim
// text, so both run the same code. Guest calls are renamed to
// __cccc_native_<name>.
//
// The text includes no headers: native output already has a host or bundled
// <string.h>/<stdlib.h> in scope. The VM defines __CCCC_C23_FREE as its
// VM-heap-aware free.
#ifndef __CCCC_C23_FREE
#define __CCCC_C23_FREE free
extern void free(void *);
#endif

static inline void *__cccc_native_memset_explicit(void *s, int c,
                                                  __SIZE_TYPE__ n) {
    volatile unsigned char *p = (volatile unsigned char *)s;
    for (__SIZE_TYPE__ i = 0; i < n; i++)
        p[i] = (unsigned char)c;
    return s;
}

static inline __SIZE_TYPE__ __cccc_native_memalignment(const void *p) {
    if (!p)
        return 0;
    return (__SIZE_TYPE__)1 << __builtin_ctzll((__UINTPTR_TYPE__)p);
}

static inline void __cccc_native_free_sized(void *ptr, __SIZE_TYPE__ size) {
    (void)size;
    __CCCC_C23_FREE(ptr);
}

static inline void __cccc_native_free_aligned_sized(void         *ptr,
                                                    __SIZE_TYPE__ alignment,
                                                    __SIZE_TYPE__ size) {
    (void)alignment;
    (void)size;
    __CCCC_C23_FREE(ptr);
}

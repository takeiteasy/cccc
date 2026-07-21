// stdlib.h stdlib function registration
#include "../cccc.h"
#include <wchar.h>
#include <string.h>
#include <errno.h>

#if defined(__APPLE__)
#include <Availability.h>
#endif

// aligned_alloc native availability: macOS 10.15+ or glibc 2.16+
#if defined(__APPLE__) && defined(__MAC_OS_X_VERSION_MIN_REQUIRED) && \
    __MAC_OS_X_VERSION_MIN_REQUIRED >= __MAC_10_15
#define CCCC_HAVE_NATIVE_ALIGNED_ALLOC 1
#elif defined(__GLIBC__)
#include <features.h>
#if __GLIBC_PREREQ(2, 16)
#define CCCC_HAVE_NATIVE_ALIGNED_ALLOC 1
#endif
#endif

#ifndef CCCC_HAVE_NATIVE_ALIGNED_ALLOC
static void *cccc_aligned_alloc(size_t alignment, size_t size) {
    void *ptr = NULL;
    if (alignment == 0 || (alignment & (alignment - 1)) != 0)
        return NULL;
    if (posix_memalign(&ptr, alignment, size) != 0)
        return NULL;
    return ptr;
}
#endif

// Block_copy implementation: heap-duplicate a stack-allocated block descriptor.
// Descriptor layout: [invoke_ptr(0) | byte_size(8) | captures...]
// Reads byte_size from slot 1, mallocs that many bytes, and copies the descriptor.
static void *cccc_block_copy_impl(void *desc) {
    if (!desc) return NULL;
    size_t size = (size_t)((long long *)desc)[1];
    void *copy = malloc(size);
    if (copy) memcpy(copy, desc, size);
    return copy;
}

// Wrapper for realloc that matches C11 semantics
static void *cccc_realloc(void *ptr, size_t size) {
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, size);
}

// Portable reallocarray polyfill (#699): not every host libc has it (e.g.
// this SDK's macOS -- glibc >= 2.26 and some BSDs do). The overflow check on
// nmemb*size is the entire point of reallocarray over realloc(ptr,
// nmemb*size); relying on a native symbol that may not exist would either
// fail to link or (worse) silently drop that check on hosts that lack it.
// Used for the --no-vm-heap FFI path; the VM-heap path (default) instead
// routes through the overflow-checked REALCA opcode in ops.c.
static void *cccc_reallocarray(void *ptr, size_t nmemb, size_t size) {
    if (size != 0 && nmemb > (SIZE_MAX / size)) {
        errno = ENOMEM;
        return NULL;
    }
    return cccc_realloc(ptr, nmemb * size);
}

// Wrappers for int-returning functions that can return negative values
static long long wrap_atoi(long long s)                      { return (long long)atoi((const char *)s); }
static long long wrap_system(long long cmd)                  { return (long long)system((const char *)cmd); }
static long long wrap_mblen(long long s, long long n)        { return (long long)mblen((const char *)s, (size_t)n); }
static long long wrap_mbtowc(long long pwc, long long s, long long n) { return (long long)mbtowc((wchar_t *)pwc, (const char *)s, (size_t)n); }
static long long wrap_wctomb(long long s, long long wc)      { return (long long)wctomb((char *)s, (wchar_t)wc); }

// C23: free_sized/free_aligned_sized - a conforming implementation may
// simply call free(), ignoring the size/alignment hints.
static void cccc_free_sized(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

static void cccc_free_aligned_sized(void *ptr, size_t alignment, size_t size) {
    (void)alignment;
    (void)size;
    free(ptr);
}

// C23: memalignment - the largest power-of-two alignment satisfied by p.
static size_t cccc_memalignment(const void *p) {
    if (!p)
        return 0;
    return (size_t)1 << __builtin_ctzll((unsigned long long)(uintptr_t)p);
}

// C23: strtol/strtoll/strtoul/strtoull accept an optional "0b"/"0B" prefix
// when base is 0 or 2.
static long long cccc_strtoll(const char *nptr, char **endptr, int base) {
    if (base == 0 || base == 2) {
        const char *p = nptr;
        while (isspace((unsigned char)*p)) p++;
        const char *digits = p;
        if (*digits == '+' || *digits == '-') digits++;
        if (digits[0] == '0' && (digits[1] == 'b' || digits[1] == 'B') &&
            (digits[2] == '0' || digits[2] == '1')) {
            size_t prefix_len = (size_t)(digits - p);
            size_t rest_len = strlen(digits + 2);
            char *tmp = malloc(prefix_len + rest_len + 1);
            memcpy(tmp, p, prefix_len);
            memcpy(tmp + prefix_len, digits + 2, rest_len + 1);
            char *tmp_end;
            long long result = strtoll(tmp, &tmp_end, 2);
            if (endptr)
                *endptr = (char *)p + (size_t)(tmp_end - tmp) + 2;
            free(tmp);
            return result;
        }
    }
    return strtoll(nptr, endptr, base);
}

static unsigned long long cccc_strtoull(const char *nptr, char **endptr, int base) {
    if (base == 0 || base == 2) {
        const char *p = nptr;
        while (isspace((unsigned char)*p)) p++;
        const char *digits = p;
        if (*digits == '+' || *digits == '-') digits++;
        if (digits[0] == '0' && (digits[1] == 'b' || digits[1] == 'B') &&
            (digits[2] == '0' || digits[2] == '1')) {
            size_t prefix_len = (size_t)(digits - p);
            size_t rest_len = strlen(digits + 2);
            char *tmp = malloc(prefix_len + rest_len + 1);
            memcpy(tmp, p, prefix_len);
            memcpy(tmp + prefix_len, digits + 2, rest_len + 1);
            char *tmp_end;
            unsigned long long result = strtoull(tmp, &tmp_end, 2);
            if (endptr)
                *endptr = (char *)p + (size_t)(tmp_end - tmp) + 2;
            free(tmp);
            return result;
        }
    }
    return strtoull(nptr, endptr, base);
}

static long cccc_strtol(const char *nptr, char **endptr, int base) {
    return (long)cccc_strtoll(nptr, endptr, base);
}

static unsigned long cccc_strtoul(const char *nptr, char **endptr, int base) {
    return (unsigned long)cccc_strtoull(nptr, endptr, base);
}

// Register all stdlib.h functions
void register_stdlib_functions(VirtualMachine *vm) {
    // Conversion functions
    cc_register_cfunc(vm, "atof", (void*)atof, 1, 1);       // returns double
    cc_register_cfunc(vm, "atoi", (void*)wrap_atoi, 1, 0);
    cc_register_cfunc(vm, "atol", (void*)atol, 1, 0);       // returns long
    cc_register_cfunc(vm, "atoll", (void*)atoll, 1, 0);     // returns long long
    cc_register_cfunc(vm, "strtod", (void*)strtod, 2, 1);   // returns double
    cc_register_cfunc(vm, "strtof", (void*)strtof, 2, 1);   // returns float (was incorrectly 0)
    cc_register_cfunc(vm, "strtold", (void*)strtold, 2, 1); // returns long double
    cc_register_cfunc(vm, "strtol", (void*)cccc_strtol, 3, 0);
    cc_register_cfunc(vm, "strtoll", (void*)cccc_strtoll, 3, 0);
    cc_register_cfunc(vm, "strtoul", (void*)cccc_strtoul, 3, 0);
    cc_register_cfunc(vm, "strtoull", (void*)cccc_strtoull, 3, 0);

    // Random number generation
    cc_register_cfunc(vm, "rand", (void*)rand, 0, 0);
    cc_register_cfunc(vm, "srand", (void*)srand, 1, 0);

    // Apple Blocks extension: heap-copy a block descriptor for escape
    cc_register_cfunc(vm, "__cccc_block_copy_impl", (void*)cccc_block_copy_impl, 1, 0);

    // Memory allocation functions
#ifdef CCCC_HAVE_NATIVE_ALIGNED_ALLOC
    cc_register_cfunc(vm, "aligned_alloc", (void*)aligned_alloc, 2, 0);
#else
    cc_register_cfunc(vm, "aligned_alloc", (void*)cccc_aligned_alloc, 2, 0);
#endif
    cc_register_cfunc(vm, "calloc", (void*)calloc, 2, 0);
    cc_register_cfunc(vm, "free", (void*)free, 1, 0);
    cc_register_cfunc(vm, "free_sized", (void*)cccc_free_sized, 2, 0);
    cc_register_cfunc(vm, "free_aligned_sized", (void*)cccc_free_aligned_sized, 3, 0);
    cc_register_cfunc(vm, "malloc", (void*)malloc, 1, 0);
    cc_register_cfunc(vm, "realloc", (void*)cccc_realloc, 2, 0);  // Use wrapper for C11 semantics
    cc_register_cfunc(vm, "reallocarray", (void*)cccc_reallocarray, 3, 0);  // Portable polyfill (#699)
    cc_register_cfunc(vm, "posix_memalign", (void*)posix_memalign, 3, 0);
    cc_register_cfunc(vm, "memalignment", (void*)cccc_memalignment, 1, 0);

    // Process control
    cc_register_cfunc(vm, "abort", (void*)abort, 0, 0);
    cc_register_cfunc(vm, "exit", (void*)exit, 1, 0);
    cc_register_cfunc(vm, "_Exit", (void*)_Exit, 1, 0);
    cc_register_cfunc(vm, "atexit", (void*)atexit, 1, 0);
    cc_register_cfunc(vm, "at_quick_exit", (void*)at_quick_exit, 1, 0);
    cc_register_cfunc(vm, "quick_exit", (void*)quick_exit, 1, 0);

    // Environment
    cc_register_cfunc(vm, "getenv", (void*)getenv, 1, 0);
    cc_register_cfunc(vm, "system", (void*)wrap_system, 1, 0);

    // Search and sort
    cc_register_cfunc(vm, "bsearch", (void*)bsearch, 4, 0);
    cc_register_cfunc(vm, "qsort", (void*)qsort, 3, 0);

    // Integer arithmetic
    cc_register_cfunc(vm, "abs", (void*)abs, 1, 1);
    cc_register_cfunc(vm, "labs", (void*)labs, 1, 1);
    cc_register_cfunc(vm, "llabs", (void*)llabs, 1, 1);
    cc_register_cfunc(vm, "div", (void*)div, 2, 0);
    cc_register_cfunc(vm, "ldiv", (void*)ldiv, 2, 0);
    cc_register_cfunc(vm, "lldiv", (void*)lldiv, 2, 0);

    // Multibyte/wide character conversion
    cc_register_cfunc(vm, "mblen", (void*)wrap_mblen, 2, 0);
    cc_register_cfunc(vm, "mbtowc", (void*)wrap_mbtowc, 3, 0);
    cc_register_cfunc(vm, "wctomb", (void*)wrap_wctomb, 2, 0);
    cc_register_cfunc(vm, "mbstowcs", (void*)mbstowcs, 3, 0);
    cc_register_cfunc(vm, "wcstombs", (void*)wcstombs, 3, 0);
}

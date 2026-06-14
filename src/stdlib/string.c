// string.h stdlib function registration
#include "../cccc.h"

// C23 memset_explicit native availability: glibc 2.39+. (Apple SDKs as of
// macOS 15 do not yet declare memset_explicit despite some references citing
// macOS 14; revisit once an Apple SDK actually exposes it.)
#if defined(__GLIBC__)
#include <features.h>
#if __GLIBC_PREREQ(2, 39)
#define CCCC_HAVE_NATIVE_MEMSET_EXPLICIT 1
#endif
#endif

// Wrapper functions for string.h to sign-extend int return values
long long wrap_strcmp(long long s1, long long s2) {
    return (long long)strcmp((const char *)s1, (const char *)s2);
}

long long wrap_strncmp(long long s1, long long s2, long long n) {
    return (long long)strncmp((const char *)s1, (const char *)s2, (size_t)n);
}

long long wrap_memcmp(long long s1, long long s2, long long n) {
    return (long long)memcmp((const void *)s1, (const void *)s2, (size_t)n);
}

#ifndef CCCC_HAVE_NATIVE_MEMSET_EXPLICIT
// C23 memset_explicit: like memset, but guaranteed not to be optimized away.
static void *cccc_memset_explicit(void *s, int c, size_t n) {
    volatile unsigned char *p = (volatile unsigned char *)s;
    for (size_t i = 0; i < n; i++)
        p[i] = (unsigned char)c;
    return s;
}
#endif

// Register all string.h functions
void register_string_functions(VirtualMachine *vm) {
    // Memory operations
    cc_register_cfunc(vm, "memcpy", (void*)memcpy, 3, 0);
    cc_register_cfunc(vm, "memmove", (void*)memmove, 3, 0);
    cc_register_cfunc(vm, "memset", (void*)memset, 3, 0);
    cc_register_cfunc(vm, "memcmp", (void*)wrap_memcmp, 3, 0);
    cc_register_cfunc(vm, "memccpy", (void*)memccpy, 4, 0);
    cc_register_cfunc(vm, "memchr", (void*)memchr, 3, 0);
#ifdef CCCC_HAVE_NATIVE_MEMSET_EXPLICIT
    cc_register_cfunc(vm, "memset_explicit", (void*)memset_explicit, 3, 0);
#else
    cc_register_cfunc(vm, "memset_explicit", (void*)cccc_memset_explicit, 3, 0);
#endif

    // String length
    cc_register_cfunc(vm, "strlen", (void*)strlen, 1, 0);

    // String comparison
    cc_register_cfunc(vm, "strcmp", (void*)wrap_strcmp, 2, 0);
    cc_register_cfunc(vm, "strncmp", (void*)wrap_strncmp, 3, 0);

    // String copying
    cc_register_cfunc(vm, "strcpy", (void*)strcpy, 2, 0);
    cc_register_cfunc(vm, "strncpy", (void*)strncpy, 3, 0);

    // String concatenation
    cc_register_cfunc(vm, "strcat", (void*)strcat, 2, 0);
    cc_register_cfunc(vm, "strncat", (void*)strncat, 3, 0);

    // String search
    cc_register_cfunc(vm, "strchr", (void*)strchr, 2, 0);
    cc_register_cfunc(vm, "strrchr", (void*)strrchr, 2, 0);
    cc_register_cfunc(vm, "strstr", (void*)strstr, 2, 0);
    cc_register_cfunc(vm, "strpbrk", (void*)strpbrk, 2, 0);

    // Other string functions
    cc_register_cfunc(vm, "strxfrm", (void*)strxfrm, 3, 0);
    cc_register_cfunc(vm, "strerror", (void*)strerror, 1, 0);
    cc_register_cfunc(vm, "strdup", (void*)strdup, 1, 0);
    cc_register_cfunc(vm, "strndup", (void*)strndup, 2, 0);
}

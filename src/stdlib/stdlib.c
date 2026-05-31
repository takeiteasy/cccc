// stdlib.h stdlib function registration
#include "../jcc.h"
#include <wchar.h>

// Wrapper for realloc that matches C11 semantics
static void *jcc_realloc(void *ptr, size_t size) {
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, size);
}

// Wrappers for int-returning functions that can return negative values
static long long wrap_atoi(long long s)                      { return (long long)atoi((const char *)s); }
static long long wrap_system(long long cmd)                  { return (long long)system((const char *)cmd); }
static long long wrap_mblen(long long s, long long n)        { return (long long)mblen((const char *)s, (size_t)n); }
static long long wrap_mbtowc(long long pwc, long long s, long long n) { return (long long)mbtowc((wchar_t *)pwc, (const char *)s, (size_t)n); }
static long long wrap_wctomb(long long s, long long wc)      { return (long long)wctomb((char *)s, (wchar_t)wc); }

// Register all stdlib.h functions
void register_stdlib_functions(JCC *vm) {
    // Conversion functions
    cc_register_cfunc(vm, "atof", (void*)atof, 1, 1);       // returns double
    cc_register_cfunc(vm, "atoi", (void*)wrap_atoi, 1, 0);
    cc_register_cfunc(vm, "atol", (void*)atol, 1, 0);       // returns long
    cc_register_cfunc(vm, "atoll", (void*)atoll, 1, 0);     // returns long long
    cc_register_cfunc(vm, "strtod", (void*)strtod, 2, 1);   // returns double
    cc_register_cfunc(vm, "strtof", (void*)strtof, 2, 1);   // returns float (was incorrectly 0)
    cc_register_cfunc(vm, "strtold", (void*)strtold, 2, 1); // returns long double
    cc_register_cfunc(vm, "strtol", (void*)strtol, 3, 0);
    cc_register_cfunc(vm, "strtoll", (void*)strtoll, 3, 0);
    cc_register_cfunc(vm, "strtoul", (void*)strtoul, 3, 0);
    cc_register_cfunc(vm, "strtoull", (void*)strtoull, 3, 0);

    // Random number generation
    cc_register_cfunc(vm, "rand", (void*)rand, 0, 0);
    cc_register_cfunc(vm, "srand", (void*)srand, 1, 0);

    // Memory allocation functions
    cc_register_cfunc(vm, "calloc", (void*)calloc, 2, 0);
    cc_register_cfunc(vm, "free", (void*)free, 1, 0);
    cc_register_cfunc(vm, "malloc", (void*)malloc, 1, 0);
    cc_register_cfunc(vm, "realloc", (void*)jcc_realloc, 2, 0);  // Use wrapper for C11 semantics
    cc_register_cfunc(vm, "posix_memalign", (void*)posix_memalign, 3, 0);

    // Process control
    cc_register_cfunc(vm, "abort", (void*)abort, 0, 0);
    cc_register_cfunc(vm, "exit", (void*)exit, 1, 0);
    cc_register_cfunc(vm, "_Exit", (void*)_Exit, 1, 0);
    cc_register_cfunc(vm, "atexit", (void*)atexit, 1, 0);

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

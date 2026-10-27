// string.h stdlib function registration
#include "../cccc.h"
#include "../internal.h" // cc_running_vm / cc_type_shadow_copy (#653)

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

// Shadow-aware memcpy/memmove (#653): a plain FFI registration would leave
// these routed through op_CALLF_fn's generic backstop, which -- lacking any
// idea of what the call actually wrote -- just clears dst's effective type.
// That's correct-but-lossy for the extremely common struct/array copy
// pattern, so these shims run the real libc call and then propagate src's
// effective type onto dst, exactly like the MCPY opcode used for compiler-
// generated copies (op_MCPY_fn, ops.c). op_CALLF_fn special-cases these two
// names to skip its backstop clear so this propagation isn't immediately
// undone.
static void *cccc_shim_memcpy(void *dst, const void *src, size_t n) {
    void *r = memcpy(dst, src, n);
    if (cc_running_vm)
        cc_type_shadow_copy(cc_running_vm, dst, src, n);
    return r;
}

static void *cccc_shim_memmove(void *dst, const void *src, size_t n) {
    void *r = memmove(dst, src, n);
    if (cc_running_vm)
        cc_type_shadow_copy(cc_running_vm, dst, src, n);
    return r;
}

// `__*_chk` entry points behind __builtin___*_chk and fortified headers: fail
// when the object size `os` is known and the copy would exceed it, then run
// the plain function.
static void *chk_memcpy(void *d, const void *s, size_t n, size_t os) {
    CCCC_FORTIFY_CHECK(n, os, "memcpy");
    return cccc_shim_memcpy(d, s, n);
}
static void *chk_memmove(void *d, const void *s, size_t n, size_t os) {
    CCCC_FORTIFY_CHECK(n, os, "memmove");
    return cccc_shim_memmove(d, s, n);
}
static void *chk_mempcpy(void *d, const void *s, size_t n, size_t os) {
    CCCC_FORTIFY_CHECK(n, os, "mempcpy");
    cccc_shim_memcpy(d, s, n);
    return (char *)d + n;
}
static void *chk_memset(void *d, int c, size_t n, size_t os) {
    CCCC_FORTIFY_CHECK(n, os, "memset");
    return memset(d, c, n);
}
static char *chk_strcpy(char *d, const char *s, size_t os) {
    CCCC_FORTIFY_CHECK(strlen(s) + 1, os, "strcpy");
    return strcpy(d, s);
}
static char *chk_stpcpy(char *d, const char *s, size_t os) {
    CCCC_FORTIFY_CHECK(strlen(s) + 1, os, "stpcpy");
    return stpcpy(d, s);
}
static char *chk_strncpy(char *d, const char *s, size_t n, size_t os) {
    CCCC_FORTIFY_CHECK(n, os, "strncpy");
    return strncpy(d, s, n);
}
static char *chk_stpncpy(char *d, const char *s, size_t n, size_t os) {
    CCCC_FORTIFY_CHECK(n, os, "stpncpy");
    return stpncpy(d, s, n);
}
static char *chk_strcat(char *d, const char *s, size_t os) {
    CCCC_FORTIFY_CHECK(strlen(d) + strlen(s) + 1, os, "strcat");
    return strcat(d, s);
}
static char *chk_strncat(char *d, const char *s, size_t n, size_t os) {
    size_t sl = strlen(s);
    CCCC_FORTIFY_CHECK(strlen(d) + (sl < n ? sl : n) + 1, os, "strncat");
    return strncat(d, s, n);
}
#ifdef __APPLE__
static size_t chk_strlcpy(char *d, const char *s, size_t n, size_t os) {
    CCCC_FORTIFY_CHECK(n, os, "strlcpy");
    return strlcpy(d, s, n);
}
static size_t chk_strlcat(char *d, const char *s, size_t n, size_t os) {
    CCCC_FORTIFY_CHECK(n, os, "strlcat");
    return strlcat(d, s, n);
}
static void *chk_memccpy(void *d, const void *s, int c, size_t n, size_t os) {
    CCCC_FORTIFY_CHECK(n, os, "memccpy");
    return memccpy(d, s, c, n);
}
#endif

// Register all string.h functions
void register_string_functions(VirtualMachine *vm) {
    // Memory operations
    cc_register_cfunc(vm, "memcpy", (void *)cccc_shim_memcpy, 3, 0);
    cc_register_cfunc(vm, "memmove", (void *)cccc_shim_memmove, 3, 0);
    cc_register_cfunc(vm, "memset", (void *)memset, 3, 0);
    cc_register_cfunc(vm, "memcmp", (void *)wrap_memcmp, 3, 0);
    cc_register_cfunc(vm, "memccpy", (void *)memccpy, 4, 0);
    cc_register_cfunc(vm, "memchr", (void *)memchr, 3, 0);

    // String length
    cc_register_cfunc(vm, "strlen", (void *)strlen, 1, 0);

    // String comparison
    cc_register_cfunc(vm, "strcmp", (void *)wrap_strcmp, 2, 0);
    cc_register_cfunc(vm, "strncmp", (void *)wrap_strncmp, 3, 0);

    // String copying
    cc_register_cfunc(vm, "strcpy", (void *)strcpy, 2, 0);
    cc_register_cfunc(vm, "strncpy", (void *)strncpy, 3, 0);

    // String concatenation
    cc_register_cfunc(vm, "strcat", (void *)strcat, 2, 0);
    cc_register_cfunc(vm, "strncat", (void *)strncat, 3, 0);

    // String search
    cc_register_cfunc(vm, "strchr", (void *)strchr, 2, 0);
    cc_register_cfunc(vm, "strrchr", (void *)strrchr, 2, 0);
    cc_register_cfunc(vm, "strstr", (void *)strstr, 2, 0);
    // strtok was declared in <string.h> but never registered here; guest
    // code that #include'd the header and called it compiled clean and then
    // failed at call time. strtok_r is the POSIX re-entrant form. Both take
    // and return real host pointers, no wrapper needed.
    cc_register_cfunc(vm, "strtok", (void *)strtok, 2, 0);
    cc_register_cfunc(vm, "strtok_r", (void *)strtok_r, 3, 0);
    cc_register_cfunc(vm, "strpbrk", (void *)strpbrk, 2, 0);
    cc_register_cfunc(vm, "strspn", (void *)strspn, 2, 0);
    cc_register_cfunc(vm, "strcspn", (void *)strcspn, 2, 0);

    // Other string functions
    cc_register_cfunc(vm, "strxfrm", (void *)strxfrm, 3, 0);
    cc_register_cfunc(vm, "strcoll", (void *)strcoll, 2, 0);
    cc_register_cfunc(vm, "strerror", (void *)strerror, 1, 0);
    cc_register_cfunc(vm, "strsignal", (void *)strsignal, 1, 0);
    cc_register_cfunc(vm, "strdup", (void *)strdup, 1, 0);
    cc_register_cfunc(vm, "strndup", (void *)strndup, 2, 0);

    // __builtin___*_chk
    cc_register_cfunc(vm, "__memcpy_chk", (void *)chk_memcpy, 4, 0);
    cc_register_cfunc(vm, "__memmove_chk", (void *)chk_memmove, 4, 0);
    cc_register_cfunc(vm, "__mempcpy_chk", (void *)chk_mempcpy, 4, 0);
    cc_register_cfunc(vm, "__memset_chk", (void *)chk_memset, 4, 0);
    cc_register_cfunc(vm, "__strcpy_chk", (void *)chk_strcpy, 3, 0);
    cc_register_cfunc(vm, "__stpcpy_chk", (void *)chk_stpcpy, 3, 0);
    cc_register_cfunc(vm, "__strncpy_chk", (void *)chk_strncpy, 4, 0);
    cc_register_cfunc(vm, "__stpncpy_chk", (void *)chk_stpncpy, 4, 0);
    cc_register_cfunc(vm, "__strcat_chk", (void *)chk_strcat, 3, 0);
    cc_register_cfunc(vm, "__strncat_chk", (void *)chk_strncat, 4, 0);
#ifdef __APPLE__
    cc_register_cfunc(vm, "__strlcpy_chk", (void *)chk_strlcpy, 4, 0);
    cc_register_cfunc(vm, "__strlcat_chk", (void *)chk_strlcat, 4, 0);
    cc_register_cfunc(vm, "__memccpy_chk", (void *)chk_memccpy, 5, 0);
#endif
}

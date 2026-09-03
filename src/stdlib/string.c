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

// C23 memset_explicit: like memset, but guaranteed not to be optimized away.
// Current Apple and glibc headers do not expose a portable native declaration,
// so keep the VM registration independent of host libc version.
static void *cccc_memset_explicit(void *s, int c, size_t n) {
    volatile unsigned char *p = (volatile unsigned char *)s;
    for (size_t i = 0; i < n; i++)
        p[i] = (unsigned char)c;
    return s;
}

// Register all string.h functions
void register_string_functions(VirtualMachine *vm) {
    // Memory operations
    cc_register_cfunc(vm, "memcpy", (void *)cccc_shim_memcpy, 3, 0);
    cc_register_cfunc(vm, "memmove", (void *)cccc_shim_memmove, 3, 0);
    cc_register_cfunc(vm, "memset", (void *)memset, 3, 0);
    cc_register_cfunc(vm, "memcmp", (void *)wrap_memcmp, 3, 0);
    cc_register_cfunc(vm, "memccpy", (void *)memccpy, 4, 0);
    cc_register_cfunc(vm, "memchr", (void *)memchr, 3, 0);
    cc_register_cfunc(vm, "memset_explicit", (void *)cccc_memset_explicit, 3,
                      0);

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
}

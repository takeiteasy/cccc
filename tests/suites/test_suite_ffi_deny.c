// CCCC_FLAGS: --testing --ffi-deny=strlen
// Consolidated suite: FFI deny-list and fatal-error behaviour
// Source tests: test_ffi_deny_zero, test_ffi_deny_dlfcn_zero,
// test_c4_ffi_rehydrate, test_ffi_fatal_error

#include <dlfcn.h>
#include <string.h>

#pragma cccc suite begin "ffi_deny"

// test_ffi_deny_zero: blocked call returns 0, not the real result
[[cccc::test(return = 42)]]
int test_ffi_deny_zero(void) {
    if (strlen("blocked") != 0)
        return 1;
    return 42;
}

// test_ffi_deny_dlfcn_zero: dlsym for a denied function returns NULL
[[cccc::test(return = 42)]]
int test_ffi_deny_dlfcn_zero(void) {
    void *handle = dlopen(0, RTLD_NOW);
    if (!handle)
        return 1;

    unsigned long (*fn)(const char *) = dlsym(handle, "strlen");
    if (!fn)
        return 42; // dlsym correctly blocked by --ffi-deny

    return 3;      // dlsym should have been blocked
}

// test_c4_ffi_rehydrate: deny policy survives a .c4 save/load round-trip
[[cccc::test(return = 42)]]
int test_c4_ffi_rehydrate(void) {
    // strlen is in the deny list: blocked call returns 0
    if (strlen("hello") != 0)
        return 1;

    // strcmp is not denied: call works normally
    if (strcmp("hello", "hello") != 0)
        return 2;

    return 42;
}

// test_ffi_fatal_error: denied call with --ffi-errors-fatal aborts (exit 255)
[[cccc::test(exit_code = 255, flags = "--ffi-errors-fatal")]]
int test_ffi_fatal_error(void) {
    return (int)strlen("fatal");
}

#pragma cccc suite end

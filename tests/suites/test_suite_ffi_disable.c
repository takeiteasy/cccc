// CCCC_FLAGS: --testing --disable-ffi
// Consolidated suite: FFI completely disabled
// Source tests: test_ffi_disable_dlfcn_zero

#include <dlfcn.h>
#include <string.h>

#pragma cccc suite begin "ffi_disable"

// test_ffi_disable_dlfcn_zero: dlsym blocked when --disable-ffi is active
[[cccc::test(return = 42)]]
int test_ffi_disable_dlfcn_zero(void) {
    void *handle = dlopen(0, RTLD_NOW);
    if (!handle)
        return 1;

    unsigned long (*fn)(const char *) = dlsym(handle, "strlen");
    if (!fn)
        return 42; // dlsym correctly blocked by --disable-ffi

    return 3; // dlsym should have been blocked
}

#pragma cccc suite end

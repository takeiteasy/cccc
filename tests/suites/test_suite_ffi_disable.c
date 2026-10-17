// CCCC_FLAGS: --testing --disable-ffi
// CCCC_NATIVE_SKIP: VM-only FFI enforcement
// Consolidated suite: FFI completely disabled
// Source tests: test_ffi_disable_dlfcn_zero

#include <dlfcn.h>

#pragma cccc suite begin "ffi_disable"

// test_ffi_disable_dlfcn_zero: dlopen fails when --disable-ffi is active
[[cccc::test(return = 42)]]
int test_ffi_disable_dlfcn_zero(void) {
    void *handle = dlopen(0, RTLD_NOW);
    if (handle)
        return 1; // dlopen should have been blocked
    return dlerror() ? 42 : 2;
}

#pragma cccc suite end

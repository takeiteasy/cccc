// Ticket V010 (#875): op_CALLN_fn's DynamicSymbol branch (a function
// pointer resolved via dlsym()) hardcoded is_variadic = 0 when calling
// cccc_call_native_function, so a dlopen'd variadic symbol was always
// prepped with ffi_prep_cif (fixed-arity) instead of ffi_prep_cif_var --
// a genuine ABI miscall on platforms where the two calling conventions
// differ (e.g. arm64 Darwin passes variadic args on the stack). This
// regression-tests the fix: dlsym a variadic libc function and call it
// through a function pointer with a variadic tail, including a double
// argument (the register-file case shared with #874).
#include <dlfcn.h>
#include <string.h>

int main(void) {
    void *handle = dlopen(0, RTLD_NOW);
    if (!handle)
        return 1;

    int (*p)(char *, const char *, ...) = dlsym(handle, "sprintf");
    if (!p)
        return 2;

    char buf[128];
    p(buf, "%d-%s-%f", 7, "hi", 3.5);
    if (strcmp(buf, "7-hi-3.500000") != 0)
        return 3;

    return 42;
}

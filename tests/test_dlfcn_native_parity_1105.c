// Ticket #1105: -c=native's dlopen/dlsym/dlclose/dlerror shim
// (serialize_dlfcn_shims, src/serialize_shims.c) must reproduce the VM's own
// dynamic-library registry policy (cccc_rt_dlclose, src/vm.c) rather than
// forwarding straight to the host's libdl -- in particular, a handle with
// any still-"live" dlsym'd symbol must refuse to dlclose, exactly as the VM
// refuses it. Regression-tests VM-vs-native parity directly (runs through
// both backends), not just that the suite compiles.
#include <dlfcn.h>
#include <stdio.h>

int main(void) {
    // A handle with a live symbol cannot be closed.
    void *handle = dlopen(0, RTLD_LAZY);
    if (!handle)
        return 1;
    void *sym = dlsym(handle, "printf");
    if (!sym)
        return 2;
    if (dlclose(handle) == 0)
        return 3;
    if (!dlerror())
        return 4;

    // A handle with no live symbols closes cleanly.
    void *handle2 = dlopen(0, RTLD_LAZY);
    if (!handle2)
        return 5;
    if (dlclose(handle2) != 0)
        return 6;

    // An invalid (already-closed) handle is rejected.
    if (dlclose(handle2) == 0)
        return 7;
    if (!dlerror())
        return 8;

    return 42;
}

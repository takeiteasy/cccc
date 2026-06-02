// JCC_FLAGS: --ffi-deny=strlen
#include <dlfcn.h>
#include <string.h>

int main(void) {
    void *handle = dlopen(0, RTLD_NOW);
    if (!handle)
        return 1;

    // strlen is in the deny list, so dlsym should fail
    unsigned long (*fn)(const char *) = dlsym(handle, "strlen");
    if (!fn)
        return 42; // dlsym correctly blocked by --ffi-deny

    return 3; // dlsym should have been blocked
}

// CCCC_FLAGS: --disable-ffi
#include <dlfcn.h>
#include <string.h>

int main(void) {
    void *handle = dlopen(0, RTLD_NOW);
    if (!handle)
        return 1;

    unsigned long (*fn)(const char *) = dlsym(handle, "strlen");
    if (!fn)
        return 42; // dlsym correctly blocked by --disable-ffi

    return 3; // dlsym should have been blocked
}

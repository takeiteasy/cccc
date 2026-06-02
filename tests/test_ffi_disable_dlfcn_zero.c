// JCC_FLAGS: --disable-ffi
#include <dlfcn.h>
#include <string.h>

int main(void) {
    void *handle = dlopen(0, RTLD_NOW);
    if (!handle)
        return 1;

    unsigned long (*fn)(const char *) = dlsym(handle, "strlen");
    if (!fn)
        return 2;

    if (fn("dynamic") != 0)
        return 3;
    return 42;
}

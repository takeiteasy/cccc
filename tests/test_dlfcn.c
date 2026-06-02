#include <dlfcn.h>

int main(void) {
    void *handle = dlopen(0, RTLD_LAZY);
    if (!handle) return 1;

    void *sym = dlsym(handle, "printf");
    if (!sym) return 2;

    if (dlclose(handle) == 0) return 3;
    return 42;
}

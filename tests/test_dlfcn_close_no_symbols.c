#include <dlfcn.h>

int main(void) {
    void *handle = dlopen(0, RTLD_NOW);
    if (!handle) return 1;
    if (dlclose(handle) != 0) return 2;
    return 42;
}

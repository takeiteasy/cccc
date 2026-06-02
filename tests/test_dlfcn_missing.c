#include <dlfcn.h>

int main(void) {
    void *handle = dlopen(0, RTLD_NOW);
    if (!handle) return 1;

    void *sym = dlsym(handle, "__jcc_symbol_that_should_not_exist__");
    if (sym) return 2;
    if (!dlerror()) return 3;
    if (dlclose(handle) != 0) return 4;
    return 42;
}

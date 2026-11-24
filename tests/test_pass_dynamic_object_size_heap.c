// CCCC_NATIVE_SKIP: __builtin_dynamic_object_size reads the VM allocation header (native.md divergences)
// A pass_dynamic_object_size argument carries a heap allocation's runtime size.
#include <stdlib.h>

static size_t dynamic(const void *const p
                      __attribute__((pass_dynamic_object_size(0)))) {
    return __builtin_dynamic_object_size(p, 0);
}

int main(void) {
    int   n  = 40;
    char *p  = malloc(n);
    int   ok = dynamic(p) == 40 && dynamic(p + 10) == 30;
    free(p);
    return ok ? 42 : 1;
}

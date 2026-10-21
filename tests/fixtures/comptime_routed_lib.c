#include @comptime < dlfcn.h>

[[cccc::comptime]]
void gen(void) {}
gen();

int helper(void) {
    return 42;
}

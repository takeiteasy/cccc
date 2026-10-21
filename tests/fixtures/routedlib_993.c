#include @comptime < dlfcn.h>

[[cccc::comptime]]
void gen(void) {}
gen();

struct Pt {
    int x;
};

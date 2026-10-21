// CCCC_FLAGS: tests/fixtures/opaque_handle_1010_smoke_use.c
// An opaque handle completed in one TU and used in another builds under
// -c=native in both input orders.
#include "fixtures/opaque_handle_1010_smoke.h"
struct DyAtoms1010Smoke {
    int x;
};
DyAtoms1010Smoke *make_atoms_1010_smoke(void) {
    static struct DyAtoms1010Smoke a;
    a.x = 42;
    return &a;
}
int get_x_1010_smoke(DyAtoms1010Smoke *t) {
    return t->x;
}

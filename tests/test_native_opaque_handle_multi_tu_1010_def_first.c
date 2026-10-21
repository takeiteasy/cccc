// CCCC_FLAGS: tests/fixtures/opaque_handle_1010_smoke_def.c
// An opaque handle completed in one TU and used in another builds under
// -c=native in both input orders.
#include "fixtures/opaque_handle_1010_smoke.h"
int main(void) {
    DyAtoms1010Smoke *t = make_atoms_1010_smoke();
    return get_x_1010_smoke(t);
}

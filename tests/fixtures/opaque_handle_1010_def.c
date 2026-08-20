// Fixture for tests/test_serialize_opaque_handle_1010.c (#1010).
//
// The TU that completes the opaque handle's struct. Listed first on the
// command line (input_files[0]) by that test's own CCCC_FLAGS, so this
// exercises #1010 defect A: the completing definition's TU is parsed
// *before* the use-site TU that only ever sees the header's forward
// declaration.
#include "opaque_handle_1010.h"

struct DyAtoms1010 {
    int x;
};

DyAtoms1010 *make_atoms_1010(void) {
    static struct DyAtoms1010 a;
    a.x = 42;
    return &a;
}

int get_x_1010(DyAtoms1010 *t) {
    return t->x;
}

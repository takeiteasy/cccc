// Fixture for tests/test_serialize_opaque_handle_1010_rev.c (#1010).
//
// A TU that only ever sees the opaque handle's header forward declaration
// -- never the completing struct definition, which lives in that test's
// own file instead. Listed first on the command line (input_files[0]) by
// that test's own CCCC_FLAGS, with the completing TU listed last, so this
// exercises #1010 defect B: the use-site TU is parsed *before* the TU that
// completes the struct.
#include "opaque_handle_1010.h"

int opaque_handle_1010_use(void) {
    DyAtoms1010 *t = make_atoms_1010();
    return get_x_1010(t);
}

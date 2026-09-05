// Fixture for tests/test_serialize_included_c_amalgam_multi_tu_1298.c
// (#1298 residual). Deliberately just a bodiless prototype for a function
// defined in tests/fixtures/amalgam_1298_ops.c -- mirrors src/internal.h's
// own bodiless declarations for functions src/vm.c defines via its
// `#include "ops.c"` amalgamation.
int amalgam_1298_add(int x);

// Fixture for tests/test_serialize_included_c_amalgam_1298.c (#1298).
//
// Deliberately a plain, external-linkage (non-static) function -- mirrors
// src/vm.c's own `#include "ops.c"` amalgamation shape, where every one of
// ops.c's functions is ordinary external linkage. #999's fix only covered a
// `static` function reached the same way (tests/fixtures/
// header_static_skip_999.h); this fixture is what proves the #1298 fix
// extends it to external linkage without regressing anything #999 didn't
// already cover.
int amalgam_1298_add(int x) {
    return x + 1;
}

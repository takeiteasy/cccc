// CCCC_FLAGS: --build
// Tests that __CCCC_BUILD_MODE__ is defined in build mode,
// and that __CCCC_COMP_MODE__ / __CCCC_TEST_MODE__ are not.
#include <stdlib.h>

[[cccc::build]]
int build_main(void) {
#ifndef __CCCC_BUILD_MODE__
    return 1;
#endif
#ifdef __CCCC_COMP_MODE__
    return 2;
#endif
#ifdef __CCCC_TEST_MODE__
    return 3;
#endif
    return 42;
}

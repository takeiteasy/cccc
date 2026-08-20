// CCCC_FLAGS: --build
// Tests that #include [[cccc::build]] includes the file in build mode.
#include[[cccc::build]] "fixtures/build_only.h"

[[cccc::build]]
int build_main(void) {
#ifndef BUILD_ONLY_LOADED
    return 1;
#endif
    return 42;
}

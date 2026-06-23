// Tests that #include [[cccc::build]] is skipped in normal compilation mode.
#include [[cccc::build]] "fixtures/build_only.h"

int main(void) {
#ifdef BUILD_ONLY_LOADED
    return 1;
#endif
    return 42;
}

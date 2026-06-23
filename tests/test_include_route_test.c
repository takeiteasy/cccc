// CCCC_FLAGS: --testing
// Tests that #include [[cccc::test]] includes the file in testing mode.
#include [[cccc::test]] "fixtures/test_only.h"

[[cccc::test]]
void test_include_loaded(void) {
#ifndef TEST_ONLY_LOADED
    AssertTrue(0);
#endif
    AssertTrue(1);
}

// CCCC_FLAGS: --testing
// Consolidated suite: --testing mode attribute behavior.
// Source tests: test_mode_enum_attr, test_mode_global_attr,
//   test_mode_macro_test, test_mode_struct_attr.

// [from test_mode_enum_attr.c]
// [[cccc::test]] on an enum is attribute-stripped: enum compiled in all modes.
#pragma cccc suite begin "test_mode/attr_strip"

[[cccc::test]]
enum ModeTestStatus {
    MODE_STATUS_OK = 0,
    MODE_STATUS_FAIL = 1,
    MODE_STATUS_SKIP = 2,
};

[[cccc::test]]
void test_annotated_enum_accessible(void) {
    enum ModeTestStatus s = MODE_STATUS_SKIP;
    AssertEq(s, 2);
    AssertEq(MODE_STATUS_OK + MODE_STATUS_FAIL + MODE_STATUS_SKIP, 3);
}

// [from test_mode_global_attr.c]
// [[cccc::test]] on a global variable is attribute-stripped.
[[cccc::test]]
int mode_test_only_global = 42;

static int mode_normal_global = 100;

[[cccc::test]]
void test_annotated_global_accessible(void) {
    AssertEq(mode_test_only_global, 42);
    AssertEq(mode_normal_global, 100);
    mode_test_only_global = 99;
    AssertEq(mode_test_only_global, 99);
}

// [from test_mode_struct_attr.c]
// [[cccc::test]] on a struct declaration is attribute-stripped.
[[cccc::test]]
struct ModeTestPoint {
    int x;
    int y;
};

[[cccc::test]]
void test_annotated_struct_accessible(void) {
    struct ModeTestPoint p = {.x = 20, .y = 22};
    AssertEq(p.x + p.y, 42);
}

#pragma cccc suite end

// [from test_mode_macro_test.c]
// __CCCC_TEST_MODE__ is defined in testing mode; others are not.
#pragma cccc suite begin "test_mode/macros"

[[cccc::test]]
void test_mode_macro(void) {
#ifndef __CCCC_TEST_MODE__
    AssertTrue(0);
#endif
#ifdef __CCCC_COMP_MODE__
    AssertTrue(0);
#endif
#ifdef __CCCC_BUILD_MODE__
    AssertTrue(0);
#endif
    AssertTrue(1);
}

#pragma cccc suite end

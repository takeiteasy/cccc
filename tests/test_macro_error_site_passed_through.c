// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: test_macro_error_site_passed_through\.c:16: +int a = LEAK\(\);
// CCCC_EXPECT_STDERR: error: undefined variable 'undeclared_d'
// CCCC_EXPECT_STDERR: note: expanded from macro 'LEAK' at .*test_macro_error_site_passed_through\.c:12:21

// A body token handed to other macros as an argument is still reported at the
// invocation it came from.

// clang-format off
#define ID(x) x
#define PASS(x) ID(x)
#define LEAK() PASS(undeclared_d)
// clang-format on

int main(void) {
    int a = LEAK();
    return a;
}

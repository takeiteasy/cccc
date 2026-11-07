// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: test_macro_error_site_nested\.c:14: +int a = OUTER\(\);
// CCCC_EXPECT_STDERR: error: undefined variable 'undeclared_c'
// CCCC_EXPECT_STDERR: note: expanded from macro 'INNER' at .*test_macro_error_site_nested\.c:10:17
// CCCC_EXPECT_STDERR: note: expanded from macro 'OUTER' at .*test_macro_error_site_nested\.c:11:17

// A token from nested macros is reported at the outermost invocation, with one
// note per expansion, innermost first.

#define INNER() undeclared_c
#define OUTER() INNER()

int main(void) {
    int a = OUTER();
    return a;
}

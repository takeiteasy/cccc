// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: test_macro_error_site_in_argument\.c:13: +int a = ID\(BAD\(\)\);
// CCCC_EXPECT_STDERR: error: undefined variable 'undeclared_b'
// CCCC_EXPECT_STDERR: note: expanded from macro 'BAD' at .*test_macro_error_site_in_argument\.c:10:15
// CCCC_REJECT_STDERR: note: expanded from macro 'ID'

// A macro invoked inside another macro's argument keeps its own expansion.

#define ID(x) x
#define BAD() undeclared_b

int main(void) {
    int a = ID(BAD());
    return a;
}

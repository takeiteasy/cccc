// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: test_macro_error_site_argument\.c:12: +int a = ID\(undeclared_a\);
// CCCC_EXPECT_STDERR: error: undefined variable 'undeclared_a'
// CCCC_REJECT_STDERR: note: expanded from macro

// A token the user wrote as a macro argument is reported where it is written,
// without a note.

#define ID(x) x

int main(void) {
    int a = ID(undeclared_a);
    return a;
}

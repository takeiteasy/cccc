// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: test_macro_error_site_funclike\.c:14: +int b = BAD\(\);
// CCCC_EXPECT_STDERR: error: undefined variable 'undeclared_b'
// CCCC_EXPECT_STDERR: note: expanded from macro 'BAD' at .*test_macro_error_site_funclike\.c:10:15
// CCCC_REJECT_STDERR: test_macro_error_site_funclike\.c:10: +#define

// A token from a macro body is reported at the invocation, with a note naming
// the macro and where the token is spelled in its definition.

#define BAD() undeclared_b

int main(void) {
    int a = 0;
    int b = BAD();
    return a + b;
}

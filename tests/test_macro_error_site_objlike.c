// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: test_macro_error_site_objlike\.c:10: +return VALUE;
// CCCC_EXPECT_STDERR: error: undefined variable 'undeclared_value'
// CCCC_EXPECT_STDERR: note: expanded from macro 'VALUE' at .*test_macro_error_site_objlike\.c:7:15
// CCCC_REJECT_STDERR: test_macro_error_site_objlike\.c:7: +#define

#define VALUE undeclared_value

int main(void) {
    return VALUE;
}

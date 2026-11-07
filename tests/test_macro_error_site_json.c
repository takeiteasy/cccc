// CCCC_NATIVE_SKIP: -c=native rejects --json
// CCCC_FLAGS: --json
// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: "line":10,
// CCCC_EXPECT_STDERR: "notes":\[\{"file":".*test_macro_error_site_json\.c","line":7,"column":15,"message":"expanded from macro 'BAD' at

#define BAD() undeclared_b

int main(void) {
    return BAD();
}

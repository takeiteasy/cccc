// CCCC_FLAGS: -m tests/suites/test_suite_arrays.c
// CCCC_EXPECT_STDOUT: get_static_array
// CCCC_REJECT_STDOUT: \.L\.\.
// CCCC_REJECT_STDOUT: unsupported expr
// -m output for the arrays suite has no dotted names and no
// unsupported-expression placeholders.
int main(void) {
    return 42;
}

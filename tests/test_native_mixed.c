// Mixed mode: VM tests and native tests in the same file.
// Verifies that TAP numbers are sequential across both runners.
// CCCC_FLAGS: --testing

[[cccc::test]]
void test_vm_one(void) {
    $assert_eq(1, 1);
}

[[cccc::test]]
void test_vm_two(void) {
    $assert_eq(2, 2);
}

[[cccc::test(mode = "native")]]
void test_native_one(void) {
    $assert_eq(3, 3);
}

[[cccc::test(mode = "native")]]
void test_native_two(void) {
    $assert_eq(4, 4);
}

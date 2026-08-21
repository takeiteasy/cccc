// CCCC_FLAGS: --testing
// #1033: exercises --testing=native's generated harness -- return-value
// assertions (int/float/string), exit_code=, and a flags= test to confirm
// it's individually SKIPped rather than breaking the whole file. Run
// through both --testing (VM) and, via tools/tests.py --native, through
// --testing=native; both backends must agree.

[[cccc::test(return = 42)]]
int test_return_int(void) {
    return 42;
}

[[cccc::test(return = 3.5)]]
double test_return_float(void) {
    return 3.5;
}

[[cccc::test(return = "ok")]]
const char *test_return_str(void) {
    return "ok";
}

[[cccc::test(exit_code = 0)]]
int test_exit_zero(void) {
    return 0;
}

[[cccc::test(exit_code = 42)]]
void test_exit_via_libc(void) {
    exit(42);
}

[[cccc::test(flags = "-O2")]]
int test_per_test_flags_skipped_natively(void) {
    return 42;
}

[[cccc::test]]
void test_assert_passes(void) {
    AssertEq(1 + 1, 2);
    AssertStrEq("a", "a");
    AssertTrue(1);
}

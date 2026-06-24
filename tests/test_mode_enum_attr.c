// CCCC_FLAGS: --testing
// [[cccc::test]] on an enum is attribute-stripped: the enum is compiled
// and accessible in all modes.

[[cccc::test]]
enum TestStatus {
    STATUS_OK = 0,
    STATUS_FAIL = 1,
    STATUS_SKIP = 2,
};

[[cccc::test]]
void test_annotated_enum_accessible(void) {
    enum TestStatus s = STATUS_SKIP;
    AssertEq(s, 2);
    AssertEq(STATUS_OK + STATUS_FAIL + STATUS_SKIP, 3);
}

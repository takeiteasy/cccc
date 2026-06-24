// CCCC_FLAGS: --testing
// [[cccc::test]] on a global variable is attribute-stripped: the global is
// compiled and accessible in all modes. There is no mode filtering.

[[cccc::test]]
int test_only_global = 42;

static int normal_global = 100;

[[cccc::test]]
void test_annotated_global_accessible(void) {
    AssertEq(test_only_global, 42);
    AssertEq(normal_global, 100);
    test_only_global = 99;
    AssertEq(test_only_global, 99);
}

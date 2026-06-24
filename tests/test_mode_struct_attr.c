// CCCC_FLAGS: --testing
// [[cccc::test]] on a struct declaration is attribute-stripped: the struct
// is compiled and accessible in all modes.

[[cccc::test]]
struct TestPoint {
    int x;
    int y;
};

[[cccc::test]]
void test_annotated_struct_accessible(void) {
    struct TestPoint p = {.x = 20, .y = 22};
    AssertEq(p.x + p.y, 42);
}

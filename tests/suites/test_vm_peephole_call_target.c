// CCCC_FLAGS: --testing
// The register an indirect call jumps through is read by the call: the copy of
// a promoted function pointer into it is not a dead definition.
typedef int (*fn_t)(int);

static int add_one(int x) {
    return x + 1;
}
static int add_two(int x) {
    return x + 2;
}

static int sum_through(int which, int n) {
    fn_t f = which ? add_two : add_one;
    int  s = 0;
    for (int i = 0; i < n; i++)
        s += f(i);
    return s;
}

[[cccc::test]]
void test_indirect_call_target(void) {
    AssertEq(sum_through(0, 3), 6);
    AssertEq(sum_through(1, 3), 9);
}

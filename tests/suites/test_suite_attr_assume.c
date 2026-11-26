// CCCC_FLAGS: --testing
// Suite: gcc's and clang's statement assumption in every spelling; the bare
// and cccc:: names stay the bounds cast.

#pragma cccc suite begin "attr_assume"

[[cccc::test]]
void test_assume_spellings_do_not_evaluate(void) {
    int n = 0;
    [[gnu::assume(++n > 0)]];
    [[__gnu__::assume(++n > 0)]];
    [[clang::assume(++n > 0)]];
    __attribute__((assume(++n > 0)));
    __attribute__((__assume__(++n > 0)));
    AssertEq(n, 0);
}

[[cccc::test]]
void test_assume_comma_expression(void) {
    int n = 1;
    [[gnu::assume((n++, n > 0))]];
    AssertEq(n, 1);
}

[[cccc::test]]
void test_assume_has_c_attribute(void) {
    AssertEq(__has_c_attribute(gnu::assume), 1);
    AssertEq(__has_c_attribute(clang::assume), 1);
    AssertEq(__has_attribute(assume), 1);
}

[[cccc::test]]
void test_assume_cast_unchanged(void) {
    int  a[4]              = {1, 2, 3, 4};
    int *p                 = a;
    int *[[cccc::array]] q = (int *[[cccc::array, cccc::bounds(a, a + 4)]])p;
    AssertEq(q[3], 4);
}

#pragma cccc suite end

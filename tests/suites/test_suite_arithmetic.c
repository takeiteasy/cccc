// CCCC_FLAGS: --testing
// Consolidated suite: basic integer arithmetic
// Source tests: test_arith2-9, test_arithmetic, test_add_30, test_arith_comprehensive,
//               test_compound_assign

#pragma cccc suite begin "arithmetic"

// ── test_arith2 / test_arith3 / test_arithmetic / test_add_30 ──
// These all test that 10 + 20 == 30 via slightly different codegen paths.
[[cccc::test]]
void test_add_10_20(void) {
    int a = 10;
    int b = 20;
    int c = a + b;
    AssertEq(c, 30);
}

// test_arith4: copy before compare
[[cccc::test]]
void test_add_copy(void) {
    int a = 10;
    int b = 20;
    int c = a + b;
    int d = c;
    AssertEq(d, 30);
}

// test_arith5: result stored as comparison value
[[cccc::test]]
void test_add_cmp_result(void) {
    int a = 10;
    int b = 20;
    int c = a + b;
    int result = c != 30;
    AssertEq(result, 0);
}

// test_arith6: if (c == 30) is the success path
[[cccc::test]]
void test_add_positive_branch(void) {
    int a = 10;
    int b = 20;
    int c = a + b;
    Assert(c == 30);
}

// test_arith7: equality stored in a variable
[[cccc::test]]
void test_add_eq_flag(void) {
    int a = 10;
    int b = 20;
    int c = a + b;
    int eq = c == 30;
    AssertEq(eq, 1);
}

// test_arith8: copy then equality flag
[[cccc::test]]
void test_add_copy_eq_flag(void) {
    int a = 10;
    int b = 20;
    int c = a + b;
    int d = c;
    int eq = d == 30;
    AssertEq(eq, 1);
}

// test_arith9 is the same as test_arith4; deduplicated above.

// test_arith_comprehensive: full +, -, *, /, % pipeline
[[cccc::test]]
void test_arithmetic_ops(void) {
    int a = 5 + 3;        // 8
    int b = 10 - 4;       // 6
    int c = 7 * 2;        // 14
    int d = 20 / 4;       // 5
    int e = 17 % 5;       // 2
    int result = a + b * c - d / e;  // 8 + 84 - 2 = 90
    AssertEq(result, 90);
}

// test_compound_assign
[[cccc::test]]
void test_compound_assign(void) {
    int x = 10; x += 32;  AssertEq(x, 42);
    int y = 50; y -= 8;   AssertEq(y, 42);
    int z = 6;  z *= 7;   AssertEq(z, 42);
    int w = 84; w /= 2;   AssertEq(w, 42);
    int m = 142; m %= 100; AssertEq(m, 42);
    int a = 63; a &= 42;  AssertEq(a, 42);
    int b = 40; b |= 2;   AssertEq(b, 42);
    int c = 50; c ^= 24;  AssertEq(c, 42);
    int d = 21; d <<= 1;  AssertEq(d, 42);
    int f = 168; f >>= 2; AssertEq(f, 42);
}

#pragma cccc suite end

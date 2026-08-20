// CCCC_FLAGS: --testing
// Consolidated suite: comparisons, logical operators, ternary, unsigned
// comparison Source tests: test_comparisons, test_cmp, test_cmp_lit,
// test_logical, test_ternary, test_ult_ule

#include <stdint.h>

#pragma cccc suite begin "comparisons"

// test_comparisons
[[cccc::test]]
void test_all_relops(void) {
    int a      = 10;
    int b      = 20;
    int eq     = (a == 10);
    int ne     = (a != b);
    int lt     = (a < b);
    int le     = (a <= 10);
    int gt     = (b > a);
    int ge     = (b >= 20);
    int result = eq + ne + lt + le + gt + ge;
    AssertEq(result, 6);
}

// test_cmp
[[cccc::test]]
void test_cmp_var(void) {
    int c = 30;
    AssertEq(c, 30);
}

// test_cmp_lit
[[cccc::test]]
void test_cmp_literal(void) {
    int x  = 30;
    int eq = x == 30;
    AssertEq(eq, 1);
}

// test_logical
[[cccc::test]]
void test_logical_ops(void) {
    int a      = 5;
    int b      = 0;
    int c      = 10;
    int and1   = (a && c);
    int and2   = (b && c);
    int and3   = (a && b);
    int or1    = (a || b);
    int or2    = (b || c);
    int or3    = (b || 0);
    int result = and1 + and2 + and3 + or1 + or2 + or3;
    AssertEq(result, 3);
}

// test_ternary
[[cccc::test]]
void test_ternary(void) {
    int a = 1 ? 10 : 20;
    AssertEq(a, 10);
    int b = 0 ? 10 : 20;
    AssertEq(b, 20);
    int c = 1 ? (0 ? 1 : 2) : 3;
    AssertEq(c, 2);
    int x = 5, y = 10;
    int max = (x > y) ? x : y;
    AssertEq(max, 10);
    int result = 2 + (x < y ? 5 : 3);
    AssertEq(result, 7);
    int grade = 85;
    int cat   = grade >= 90 ? 4 : grade >= 80 ? 3 : grade >= 70 ? 2 : 1;
    AssertEq(cat, 3);
}

// test_ult_ule: unsigned 64-bit comparison (ULT3/ULE3 opcodes)
[[cccc::test(flags = "--optimize=2")]]
void test_unsigned_comparisons(void) {
#include <stdint.h>
    volatile uint64_t large = 18446744073709551614ULL;
    volatile uint64_t zero  = 0ULL;
    volatile uint64_t one   = 1ULL;
    volatile uint64_t two   = 2ULL;
    AssertFalse(large < zero);
    AssertTrue(one < two);
    AssertFalse(two < two);
    AssertFalse(large <= zero);
    AssertTrue(two <= two);
    AssertTrue(one <= two);
    volatile uint64_t max = 18446744073709551615ULL;
    AssertFalse(max < large);
    AssertTrue(large < max);
    // constant-fold path
    AssertFalse(18446744073709551614ULL < 0ULL);
    AssertTrue(1ULL < 2ULL);
    AssertFalse(2ULL < 2ULL);
    AssertFalse(18446744073709551614ULL <= 0ULL);
    AssertTrue(2ULL <= 2ULL);
    AssertTrue(1ULL <= 2ULL);
}

#pragma cccc suite end

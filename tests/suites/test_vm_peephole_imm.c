// CCCC_FLAGS: --testing
// A constant operand folded into its instruction (a constant expression, a
// switch's comparisons) computes what the register form did, with the constant
// on either side and for negative, 32-bit and 64-bit values.

#include <limits.h>

int idx_s(int c) {
    switch (c) {
        case -5:
            return 1;
        case 0:
            return 2;
        case 7:
            return 3;
        case 100:
            return 4;
        case INT_MAX:
            return 5;
        case INT_MIN:
            return 6;
        default:
            return 0;
    }
}

int idx_u(unsigned c) {
    switch (c) {
        case 3u:
            return 1;
        case 2147483648u:
            return 2;
        case 4294967295u:
            return 3;
        default:
            return 0;
    }
}

int idx_l(long c) {
    switch (c) {
        case -1L:
            return 1;
        case 2147483647L:
            return 2;
        case -2147483647L:
            return 3;
        default:
            return 0;
    }
}

[[cccc::test]]
void test_switch_int(void) {
    AssertEq(idx_s(-5), 1);
    AssertEq(idx_s(0), 2);
    AssertEq(idx_s(7), 3);
    AssertEq(idx_s(100), 4);
    AssertEq(idx_s(INT_MAX), 5);
    AssertEq(idx_s(INT_MIN), 6);
    AssertEq(idx_s(-6), 0);
    AssertEq(idx_s(8), 0);
    AssertEq(idx_s(99), 0);
    AssertEq(idx_s(101), 0);
}

[[cccc::test]]
void test_switch_unsigned(void) {
    AssertEq(idx_u(3u), 1);
    AssertEq(idx_u(2147483648u), 2);
    AssertEq(idx_u(4294967295u), 3);
    AssertEq(idx_u(0u), 0);
    AssertEq(idx_u(2u), 0);
    AssertEq(idx_u(2147483647u), 0);
}

[[cccc::test]]
void test_switch_long(void) {
    AssertEq(idx_l(-1L), 1);
    AssertEq(idx_l(2147483647L), 2);
    AssertEq(idx_l(-2147483647L), 3);
    AssertEq(idx_l(4294967296L), 0);
    AssertEq(idx_l(-4294967297L), 0);
    AssertEq(idx_l(0L), 0);
}

#define TWO     (1 + 1)
#define SIXTEEN (1 << 4)

int below(int x) { return x < -SIXTEEN; }
int above(int x) { return -SIXTEEN < x; }
int at_most(int x) { return x <= TWO * 3; }
int at_least(int x) { return TWO * 3 >= x; }
int ult(unsigned long x) { return x < (1UL << 40); }
int ugt(unsigned long x) { return (1UL << 40) < x; }
int eq(int x) { return x == 3 * 7; }
int ne(int x) { return 3 * 7 != x; }

[[cccc::test]]
void test_compare_constant_side(void) {
    AssertEq(below(-17), 1);
    AssertEq(below(-16), 0);
    AssertEq(above(-15), 1);
    AssertEq(above(-16), 0);
    AssertEq(at_most(6), 1);
    AssertEq(at_most(7), 0);
    AssertEq(at_least(6), 1);
    AssertEq(at_least(7), 0);
    AssertEq(ult((1UL << 40) - 1), 1);
    AssertEq(ult(1UL << 40), 0);
    AssertEq(ult(~0UL), 0);
    AssertEq(ugt((1UL << 40) + 1), 1);
    AssertEq(ugt(1UL << 40), 0);
    AssertEq(ugt(0UL), 0);
    AssertEq(eq(21), 1);
    AssertEq(eq(22), 0);
    AssertEq(ne(21), 0);
    AssertEq(ne(22), 1);
}

int mask(int x) { return x & (TWO + 1); }
int plus(int x) { return x + (SIXTEEN - 1); }
int minus(int x) { return x - (SIXTEEN * 2); }
int shl(unsigned x) { return x << (SIXTEEN - 15); }
int shr(int x) { return x >> (SIXTEEN - 12); }
long lshl(long x) { return x << (SIXTEEN * 4 - 1); }

[[cccc::test]]
void test_arithmetic_constant_expression(void) {
    AssertEq(mask(7), 3);
    AssertEq(mask(4), 0);
    AssertEq(plus(1), 16);
    AssertEq(plus(-15), 0);
    AssertEq(minus(40), 8);
    AssertEq(minus(0), -32);
    AssertEq(shl(3u), 6);
    AssertEq(shr(-64), -4);
    AssertEq(lshl(1L), LONG_MIN);
}

int *last3(int *p, int n) { return p + n - 3; }
long long *last1(long long *p, int n) { return p + n - 1; }

[[cccc::test]]
void test_pointer_step_back(void) {
    int       a[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    long long b[4] = {10, 11, 12, 13};
    AssertEq(*last3(a, 6), 3);
    AssertEq(*last3(a, 3), 0);
    AssertEq(*last1(b, 4), 13);
    AssertEq(*last1(b, 1), 10);
}

// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: : ADDSX3
// CCCC_EXPECT_STDOUT: : ADDZX3
// CCCC_EXPECT_STDOUT: : SUBSX3
// CCCC_EXPECT_STDOUT: : SUBZX3
// CCCC_EXPECT_STDOUT: : MULSX3
// CCCC_EXPECT_STDOUT: : MULZX3
// CCCC_EXPECT_STDOUT: : ADDISX3
// CCCC_EXPECT_STDOUT: : ADDIZX3
// CCCC_EXPECT_STDOUT: : MULISX3
// CCCC_EXPECT_STDOUT: : MULIZX3
// CCCC_EXPECT_STDOUT: : SHLISX3
// CCCC_EXPECT_STDOUT: : SHLIZX3
// A 32-bit `int` or `unsigned` operation and the wrap after it is one opcode.
// Each result must be the value the type denotes, including when the
// destination is one of the sources.
#include <limits.h>

typedef long long ll;

[[gnu::noinline]] int add_s(int a, int b) {
    return a + b;
}
[[gnu::noinline]] unsigned add_z(unsigned a, unsigned b) {
    return a + b;
}
[[gnu::noinline]] int sub_s(int a, int b) {
    return a - b;
}
[[gnu::noinline]] unsigned sub_z(unsigned a, unsigned b) {
    return a - b;
}
[[gnu::noinline]] int mul_s(int a, int b) {
    return a * b;
}
[[gnu::noinline]] unsigned mul_z(unsigned a, unsigned b) {
    return a * b;
}
[[gnu::noinline]] int addi_s(int a) {
    return a + 100000;
}
[[gnu::noinline]] unsigned addi_z(unsigned a) {
    return a + 100000u;
}
[[gnu::noinline]] int muli_s(int a) {
    return a * 100003;
}
[[gnu::noinline]] unsigned muli_z(unsigned a) {
    return a * 100003u;
}
[[gnu::noinline]] int shli_s(int a) {
    return a << 7;
}
[[gnu::noinline]] unsigned shli_z(unsigned a) {
    return a << 7;
}

// A promoted accumulator is both a source and the destination.
[[gnu::noinline]] int acc_s(int n, int step) {
    int s = INT_MAX - 5;
    for (int i = 0; i < n; i++)
        s += step;
    return s;
}

[[gnu::noinline]] unsigned acc_z(unsigned n, unsigned step) {
    unsigned s = 5;
    for (unsigned i = 0; i < n; i++)
        s -= step;
    return s;
}

[[gnu::noinline]] int acc_mul(int n) {
    int s = 3;
    for (int i = 0; i < n; i++)
        s *= 7;
    return s;
}

int main(void) {
    if (add_s(INT_MAX, 1) != INT_MIN || add_s(-5, 3) != -2)
        return 1;
    if (add_z(UINT_MAX, 2) != 1u || add_z(1, 2) != 3u)
        return 2;
    if (sub_s(INT_MIN, 1) != INT_MAX || sub_s(3, 5) != -2)
        return 3;
    if (sub_z(0, 1) != UINT_MAX || sub_z(5, 3) != 2u)
        return 4;
    if (mul_s(65536, 65536) != 0 || mul_s(-3, 4) != -12 ||
        mul_s(INT_MAX, 2) != -2)
        return 5;
    if (mul_z(65536, 65537) != 65536u || mul_z(3, 4) != 12u)
        return 6;
    if (addi_s(INT_MAX) != (int)(INT_MAX + 100000u) || addi_s(-100000) != 0)
        return 7;
    if (addi_z(UINT_MAX) != 99999u)
        return 8;
    if (muli_s(INT_MAX) != (int)(INT_MAX * 100003u) || muli_s(-2) != -200006)
        return 9;
    if (muli_z(UINT_MAX) != (unsigned)(UINT_MAX * 100003u))
        return 10;
    if (shli_s(INT_MAX) != (int)((unsigned)INT_MAX << 7) || shli_s(-1) != -128)
        return 11;
    if (shli_z(0x02000000u) != 0u || shli_z(1) != 128u)
        return 12;
    if ((ll)add_s(INT_MAX, 1) != (ll)INT_MIN || (ll)add_z(UINT_MAX, 1) != 0)
        return 13;
    if (acc_s(10, 1) != (int)(INT_MAX - 5 + 10u))
        return 14;
    if (acc_z(10, 1) != (unsigned)(5u - 10u))
        return 15;
    if (acc_mul(20) != (int)(3u * 79792266297612001u))
        return 16;
    return 42;
}

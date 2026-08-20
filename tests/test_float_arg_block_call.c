// Regression test for #712's companion bug: GNU/Apple block invocation
// (ND_BLOCK_CALL) placed float and integer user arguments using a single
// combined counter starting at A1, but the block's own ENT3 prologue spills
// incoming params using independent int/float register counters (matching
// every other call path in the compiler). Any block taking a float/double
// parameter after -- or instead of -- an int parameter read back garbage.
// Fixed alongside #712 since it lives in the same call-marshalling code.
#include <stdio.h>

int main(void) {
    double a[2];
    a[0]                      = 3.5;

    int (^only_float)(double) = ^(double d) {
      return d == 3.5 ? 1 : 0;
    };
    if (!only_float(a[0]))
        return 1;

    int (^int_then_float)(int, double) = ^(int n, double d) {
      return (n == 7 && d == 3.5) ? 1 : 0;
    };
    if (!int_then_float(7, a[0]))
        return 2;

    int (^float_then_int)(double, int) = ^(double d, int n) {
      return (d == 3.5 && n == 7) ? 1 : 0;
    };
    if (!float_then_int(a[0], 7))
        return 3;

    int (^two_ints)(int, int) = ^(int n, int m) {
      return (n == 7 && m == 5) ? 1 : 0;
    };
    if (!two_ints(7, 5))
        return 4;

    return 42;
}

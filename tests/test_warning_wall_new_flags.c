// CCCC_FLAGS: -Wall -Wno-unused -Wno-return-type -Wno-implicit-int -Wno-shadow -Wno-discarded-qualifiers
// CCCC_EXPECT_STDERR: \[-Wmultichar\]
// CCCC_EXPECT_STDOUT: done

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

// -Wmain: void return type
void main(void) {
    // -Wmultichar: multi-character constant
    int mc = 'ab';
    (void)mc;

    // -Wswitch-default: no default case
    int x = 1;
    switch (x) {
        case 1: break;
    }

    // -Wswitch-bool: boolean condition
    bool b = true;
    switch (b) {
        case true: break;
    }

    // -Wfloat-equal: == on doubles
    double d1 = 1.0, d2 = 2.0;
    (void)(d1 == d2);

    // -Wshift-negative-value: shift by negative constant
    (void)(1 << -1);

    // -Wshift-overflow: shift amount >= type width
    (void)(1 << 32);

    // -Wlogical-op: constant operand in &&
    (void)(x && 1);

    // -Wtautological-compare: unsigned >= 0
    unsigned u = 5;
    (void)(u >= 0);

    // -Wsizeof-pointer-memaccess: sizeof(ptr) to memset
    char buf[8];
    char *p = buf;
    memset(p, 0, sizeof(p));

    // -Wswitch: enum switch missing a case, no default
    typedef enum Color { R, G, B } Color;
    Color c = R;
    switch (c) {
        case R: break;
        case G: break;
    }

    // -Wenum-compare: different named enums compared
    typedef enum Dir { UP, DOWN } Dir;
    Color c2 = R;
    Dir dir = UP;
    (void)(c2 == dir);

    // -Wincompatible-pointer-types: int* assigned from char*
    int x2 = 0;
    char *cp = (char *)&x2;
    int *ip2 = cp;
    (void)ip2;

    printf("done\n");
}

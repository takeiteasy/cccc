// CCCC_FLAGS: -Wall -Wno-unused -Wno-return-type -Wno-implicit-int -Wno-shadow
// CCCC_EXPECT_STDERR: \[-Wmultichar\]
// CCCC_EXPECT_STDOUT: done

#include <stdbool.h>
#include <stdio.h>

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

    printf("done\n");
}

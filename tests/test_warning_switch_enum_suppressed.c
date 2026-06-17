// CCCC_FLAGS: -Wswitch-enum -Wno-switch-enum
// CCCC_EXPECT_STDOUT: done

#include <stdio.h>

typedef enum { A, B, C } Color;

int describe(Color c) {
    switch (c) {
        case A: return 1;
        case B: return 2;
        default: return 0;
    }
}

int main(void) {
    (void)describe(A);
    printf("done\n");
    return 42;
}

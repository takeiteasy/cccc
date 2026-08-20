// Tests the C23 [[gnu::constructor]] / [[gnu::destructor]] spelling,
// including an explicit priority argument.
// CCCC_EXPECT_STDOUT: dtor ran

#include <stdio.h>

int g = 0;

[[gnu::constructor(150)]] void ctor(void) {
    g = 7;
}

[[gnu::destructor]] void dtor(void) {
    printf("dtor ran\n");
}

int main(void) {
    if (g != 7)
        return 1;
    return 42;
}

// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: index__cccc_dup
// CCCC_REJECT_STDOUT: static int index\(int base
// -m output renames the static function that collides with libc.

#include <stdio.h>

static int index(int base, int step) {
    return base + step * 2;
}

int main(void) {
    int r = index(10, 16);
    if (r != 42)
        return 1;
    return 42;
}

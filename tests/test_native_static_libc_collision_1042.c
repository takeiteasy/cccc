// A static function named like a libc function round-trips under -c=native.

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

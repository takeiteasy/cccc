// CCCC_FLAGS: --std=c23
// unreachable() macro in <stddef.h> (ticket #390)
#include <stddef.h>

static int classify(int x) {
    if (x > 0) return 1;
    if (x < 0) return -1;
    if (x == 0) return 0;
    unreachable();
}

int main(void) {
    if (classify(5) != 1) return 1;
    if (classify(-5) != -1) return 2;
    if (classify(0) != 0) return 3;
    return 42;
}

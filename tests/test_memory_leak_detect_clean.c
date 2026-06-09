// CCCC_FLAGS: --memory-leak-detection -V
// Clean leak detection test — all allocations freed, no leak reported
#include <stdlib.h>

int main() {
    int *a = malloc(sizeof(int) * 4);
    char *b = malloc(32);
    a[0] = 1;
    b[0] = 'x';
    free(a);
    free(b);
    return 42;
}

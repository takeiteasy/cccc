// CCCC_FLAGS: --heap-canaries -V
// Clean heap canary test — alloc/free with no overflow should succeed
#include <stdlib.h>

int main() {
    int *p = malloc(sizeof(int) * 4);
    p[0] = 1;
    p[1] = 2;
    p[2] = 3;
    p[3] = 4;
    free(p);

    char *s = malloc(16);
    s[0] = 'h';
    s[15] = '\0';
    free(s);

    return 42;
}

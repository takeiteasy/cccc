// CCCC_FLAGS: --memory-poisoning -V
// Memory poisoning test — malloc/free with poisoning enabled should not crash
#include <stdlib.h>
#include <string.h>

int main() {
    char *p = malloc(64);
    // Write something so we know it's writable
    memset(p, 0, 64);
    p[0] = 'z';
    free(p);

    // Allocate again — poisoned old block is not reused (bump allocator)
    int *q = malloc(sizeof(int) * 4);
    q[0] = 1;
    q[1] = 2;
    free(q);

    return 42;
}

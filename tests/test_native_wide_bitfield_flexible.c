// A flexible array member of a struct with a _BitInt(N>128) bit-field is
// indexed past the aggregate's own bytes.
#include <stdlib.h>

struct WithFlexible {
    int n;
    _BitInt(256) f : 100;
    int tail[];
};

int main(void) {
    struct WithFlexible *p = malloc(sizeof(*p) + 4 * sizeof(int));
    p->n                   = 4;
    p->f                   = 123;
    for (int i = 0; i < 4; i++)
        p->tail[i] = i * 10;
    int *q = &p->tail[2];
    int  r = p->n == 4 && p->f == 123 && p->tail[3] == 30 && *q == 20;
    free(p);
    return r ? 42 : 1;
}

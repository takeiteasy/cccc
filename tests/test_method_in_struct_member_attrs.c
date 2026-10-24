// A member with a custom @attribute is still a member; a method beside it
// works.
#include <cccc/derive.h>
#include <stdlib.h>

@clone @destroy typedef struct Buf {
    int  n;
    int *vals @owned("n");
    int total(Buf *self) {
        int s = 0;
        for (int i = 0; i < self->n; i++)
            s += self->vals[i];
        return s;
    }
} Buf;

int main(void) {
    Buf b     = {3, malloc(3 * sizeof(int))};
    b.vals[0] = 10;
    b.vals[1] = 12;
    b.vals[2] = 20;
    int t     = b.total();
    Buf_destroy(&b);
    return t;
}

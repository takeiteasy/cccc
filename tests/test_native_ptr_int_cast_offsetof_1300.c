// A pointer-to-integer cast used as an offsetof round-trips under -c=native.
#include <stddef.h>
typedef struct {
    long sig;
    char opaque[8];
} ptr_int_cast_1300_smoke_t;
static int compute(long long base) {
    int *slot =
        (int *)((char *)base + offsetof(ptr_int_cast_1300_smoke_t, opaque));
    return *slot;
}
int main(void) {
    ptr_int_cast_1300_smoke_t v = {0, {42, 0, 0, 0, 0, 0, 0, 0}};
    return compute((long long)&v);
}

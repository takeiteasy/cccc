// atomic_fetch_* round-trips under -c=native.
#include <stdatomic.h>
int main(void) {
    _Atomic int x   = 10;
    int         old = atomic_fetch_add(&x, 5);
    if (old != 10 || atomic_load(&x) != 15)
        return 1;
    old = atomic_fetch_sub(&x, 3);
    if (old != 15 || atomic_load(&x) != 12)
        return 2;
    return 42;
}

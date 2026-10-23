// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: __atomic_fetch_add
// CCCC_EXPECT_STDOUT: __atomic_fetch_sub
// CCCC_REJECT_STDOUT: __atomic_store_n
// -m output lowers atomic_fetch_* to one fetch-and-op builtin, not a
// load-then-store.
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

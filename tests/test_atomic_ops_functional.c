// Functional test for stdatomic.h operations: atomic_load, atomic_store,
// atomic_exchange, atomic_compare_exchange_strong/weak.
// Single-threaded — verifies correct values, no diagnostics expected.
#include <stdatomic.h>
#include <stdio.h>

int main(void) {
    atomic_int x = 0;

    // load/store round-trip
    atomic_store(&x, 7);
    if (atomic_load(&x) != 7)
        return 1;

    // explicit forms
    atomic_store_explicit(&x, 3, memory_order_relaxed);
    if (atomic_load_explicit(&x, memory_order_relaxed) != 3)
        return 2;

    // atomic_exchange: returns old value, stores new
    int old = atomic_exchange(&x, 99);
    if (old != 3 || atomic_load(&x) != 99)
        return 3;

    // compare_exchange_strong: success case
    int expected = 99;
    int r = atomic_compare_exchange_strong(&x, &expected, 42);
    if (!r || atomic_load(&x) != 42)
        return 4;

    // compare_exchange_strong: failure case — expected gets updated to current
    expected = 0; // wrong
    r = atomic_compare_exchange_strong(&x, &expected, 0);
    if (r || expected != 42 || atomic_load(&x) != 42)
        return 5;

    // compare_exchange_weak: success case
    expected = 42;
    r = atomic_compare_exchange_weak(&x, &expected, 10);
    if (!r || atomic_load(&x) != 10)
        return 6;

    // fetch_add / fetch_sub
    atomic_fetch_add(&x, 5);
    if (atomic_load(&x) != 15)
        return 7;
    atomic_fetch_sub(&x, 3);
    if (atomic_load(&x) != 12)
        return 8;

    // atomic_flag
    atomic_flag f = ATOMIC_FLAG_INIT(0);
    if (atomic_flag_test_and_set(&f))
        return 9; // first set should return 0 (was clear)
    if (!atomic_flag_test_and_set(&f))
        return 10; // second set should return 1 (was set)
    atomic_flag_clear(&f);
    if (atomic_flag_test_and_set(&f))
        return 11; // after clear, should return 0 again

    return 42; // CCCC test convention: 42 = pass
}

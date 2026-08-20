// CCCC_FLAGS: --testing
// Consolidated suite: C11 atomic operations
// Source tests: test_atomic_fetch_ops, test_atomic_ops_functional

#include <stdatomic.h>
#include <stdio.h>

// [from test_atomic_fetch_ops]
// Expected return: 42

// [from test_atomic_ops_functional]
// Functional test for stdatomic.h operations: atomic_load, atomic_store,
// atomic_exchange, atomic_compare_exchange_strong/weak.
// Single-threaded — verifies correct values, no diagnostics expected.

#pragma cccc suite begin "atomics"

// test_atomic_fetch_ops
[[cccc::test(return = 42)]]
int test_atomic_fetch_ops(void) {
    _Atomic int x   = 10;

    int         old = atomic_fetch_add(&x, 5);
    if (old != 10)
        return 1;
    if (atomic_load(&x) != 15)
        return 2;

    old = atomic_fetch_sub(&x, 3);
    if (old != 15)
        return 3;
    if (atomic_load(&x) != 12)
        return 4;

    old = atomic_fetch_or(&x, 0x3);
    if (old != 12)
        return 5;
    if (atomic_load(&x) != 15)
        return 6; // 12|3 = 15

    old = atomic_fetch_and(&x, 0xc);
    if (old != 15)
        return 7;
    if (atomic_load(&x) != 12)
        return 8; // 15&12 = 12

    old = atomic_fetch_xor(&x, 0xf);
    if (old != 12)
        return 9;
    if (atomic_load(&x) != 3)
        return 10; // 0b1100^0b1111=0b0011=3

    return 42;
}

// test_atomic_ops_functional
[[cccc::test(return = 42)]]
int test_atomic_ops_functional(void) {
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
    int r        = atomic_compare_exchange_strong(&x, &expected, 42);
    if (!r || atomic_load(&x) != 42)
        return 4;

    // compare_exchange_strong: failure case — expected gets updated to current
    expected = 0; // wrong
    r        = atomic_compare_exchange_strong(&x, &expected, 0);
    if (r || expected != 42 || atomic_load(&x) != 42)
        return 5;

    // compare_exchange_weak: success case
    expected = 42;
    r        = atomic_compare_exchange_weak(&x, &expected, 10);
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
        return 9;  // first set should return 0 (was clear)
    if (!atomic_flag_test_and_set(&f))
        return 10; // second set should return 1 (was set)
    atomic_flag_clear(&f);
    if (atomic_flag_test_and_set(&f))
        return 11; // after clear, should return 0 again

    return 42;     // CCCC test convention: 42 = pass
}

// #985: re-run the two functional cases above at -2 here (pinned via
// flags = "-2", not CCCC_FLAGS:, so only these two get bounds checks) as a
// false-positive canary across the whole atomic surface -- proof that
// adding CHKD to ALDR/ASTR/AXCHG/ACAS doesn't false-positive on any of
// their legitimate, in-bounds uses (stack locals, atomic_flag, all four
// fetch_* ops, both compare_exchange forms).
[[cccc::test(return = 42, flags = "-2")]]
int test_atomic_fetch_ops_bounds_checked(void) {
    return test_atomic_fetch_ops();
}

[[cccc::test(return = 42, flags = "-2")]]
int test_atomic_ops_functional_bounds_checked(void) {
    return test_atomic_ops_functional();
}

#pragma cccc suite end

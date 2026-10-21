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

    // atomic_flag -- atomic_flag_clear used to expand to a plain, untagged
    // `*(obj) = 0` (non-atomic on both backends, invisible to #447 mixed-
    // access detection); now routes through __builtin_atomic_store like
    // atomic_flag_test_and_set already does (#1184).
    atomic_flag f = ATOMIC_FLAG_INIT;
    if (atomic_flag_test_and_set(&f))
        return 9;  // first set should return 0 (was clear)
    if (!atomic_flag_test_and_set(&f))
        return 10; // second set should return 1 (was set)
    atomic_flag_clear(&f);
    if (atomic_flag_test_and_set(&f))
        return 11; // after clear, should return 0 again

    return 42;     // CCCC test convention: 42 = pass
}

// #1188: atomic_thread_fence/atomic_signal_fence. This is an *exercise*
// test, not a proof of correctness -- on x86-64's TSO memory model, and
// under the VM's GIL, this would compile and return 42 even with the old
// no-op fence macros. The actual proof that a real __atomic_*_fence is
// emitted lives in tests/test_serialize_atomic_fence_1188_m.c, which greps the
// -m output. This test only confirms every memory_order value, plus a
// non-constant order (C11 does not require order to be a constant
// expression), compiles and runs on both backends.
[[cccc::test(return = 42)]]
int test_atomic_fence(void) {
    _Atomic int x = 0;

    atomic_thread_fence(memory_order_relaxed);
    atomic_thread_fence(memory_order_consume);
    atomic_thread_fence(memory_order_acquire);
    atomic_thread_fence(memory_order_release);
    atomic_thread_fence(memory_order_acq_rel);
    atomic_thread_fence(memory_order_seq_cst);

    atomic_signal_fence(memory_order_relaxed);
    atomic_signal_fence(memory_order_consume);
    atomic_signal_fence(memory_order_acquire);
    atomic_signal_fence(memory_order_release);
    atomic_signal_fence(memory_order_acq_rel);
    atomic_signal_fence(memory_order_seq_cst);

    // Non-constant order: exercises the pass-through (not const-folded)
    // serializer arm.
    volatile int order = memory_order_seq_cst;
    atomic_thread_fence(order);
    atomic_signal_fence(order);

    atomic_store(&x, 7);
    atomic_thread_fence(memory_order_release);
    if (atomic_load(&x) != 7)
        return 1;

    return 42;
}

// #985: re-run the two functional cases above at -2 here (pinned via the
// per-test flags = "-2" attribute, not a header CCCC_FLAGS directive, so
// only these two get bounds checks) as a
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

// A call inside an operand of an atomic store/exchange/CAS must not clobber
// the other operand.
static unsigned call_id(unsigned v) {
    return v;
}

static int *call_ptr(int *p) {
    return p;
}

static int call_idx(void) {
    return 2;
}

[[cccc::test(return = 42)]]
int test_atomic_operand_with_call(void) {
    _Atomic unsigned c[3] = {0, 0, 0};

    atomic_store_explicit(&c[2], call_id(0x10000003u), memory_order_relaxed);
    if (atomic_load(&c[0]) != 0 || atomic_load(&c[1]) != 0 ||
        atomic_load(&c[2]) != 0x10000003u)
        return 1;

    atomic_store(&c[call_idx() - 1], call_id(7));
    if (atomic_load(&c[1]) != 7)
        return 2;

    atomic_store(&c[call_idx()], call_id(9));
    if (atomic_load(&c[2]) != 9)
        return 3;

    unsigned old = atomic_exchange(&c[0], call_id(5));
    if (old != 0 || atomic_load(&c[0]) != 5)
        return 4;

    int            x;
    _Atomic(int *) p = 0;
    atomic_exchange(&p, call_ptr(&x));
    if (atomic_load(&p) != &x)
        return 5;

    unsigned expected = 9;
    if (!atomic_compare_exchange_strong(&c[2], &expected, call_id(11)) ||
        atomic_load(&c[2]) != 11)
        return 6;

    return 42;
}

// A static inline atomic helper must not clobber the caller's frame.
static inline unsigned inline_bump(_Atomic unsigned *p) {
    return atomic_fetch_add_explicit(p, 1u, memory_order_relaxed);
}

static void inline_bump_step(_Atomic unsigned *counters, int *out, unsigned i) {
    inline_bump(&counters[i]);
    out[i] = 7;
}

[[cccc::test(return = 42)]]
int test_inline_atomic_helper_keeps_caller_frame(void) {
    _Atomic unsigned counters[2] = {0, 0};
    int              out[2]      = {0, 0};
    inline_bump_step(counters, out, 1);
    if (out[1] != 7 || atomic_load(&counters[1]) != 1)
        return 1;
    return 42;
}

// Discarded and value-position `_Atomic` increments agree.
[[cccc::test(return = 42)]]
int test_atomic_incdec_discarded_and_value(void) {
    _Atomic int  ai = 0;
    _Atomic int *pa = &ai;
    ai++;
    ++ai;
    ai--;
    --ai;
    ai += 3;
    (*pa)++;
    int j = ai++;
    int i = 0;
    ai++, ai++;
    i++, i++;
    if (j != 4 || ai != 7 || i != 2)
        return 1;
    return 42;
}

// Value-position `_Atomic` ++/-- yield the right operand value for every
// operand shape.
[[cccc::test(return = 42)]]
int test_atomic_incdec_value_forms(void) {
    _Atomic int           ai     = 5;
    _Atomic unsigned char uc     = 255;
    int                   arr[3] = {1, 2, 3};
    _Atomic(int *)        ap     = arr;
    int                   a      = ai++;
    int                   b      = ai--;
    int                   c      = ai++ + ai++;
    int                   d      = ++ai;
    unsigned char         u      = uc++;
    int                  *q      = ap++;
    if (a != 5 || b != 6 || c != 11 || d != 8 || ai != 8)
        return 1;
    if (u != 255 || uc != 0 || q != arr || ap != arr + 1)
        return 2;
    return 42;
}

// Legacy GCC __sync_* builtins: fetch/op, CAS, test_and_set, release, fence.
[[cccc::test(return = 42)]]
int test_sync_builtins(void) {
    int x = 10;
    if (__sync_fetch_and_add(&x, 5) != 10 || x != 15)
        return 1;
    if (__sync_add_and_fetch(&x, 5) != 20 || x != 20)
        return 2;
    if (__sync_fetch_and_sub(&x, 4) != 20 || __sync_sub_and_fetch(&x, 4) != 12)
        return 3;
    if (__sync_fetch_and_or(&x, 3) != 12 || x != 15)
        return 4;
    if (__sync_and_and_fetch(&x, 6) != 6 || __sync_xor_and_fetch(&x, 3) != 5)
        return 5;
    x = 6;
    if (__sync_fetch_and_nand(&x, 3) != 6 || x != ~(6 & 3))
        return 6;
    x = 6;
    if (__sync_nand_and_fetch(&x, 3) != ~(6 & 3))
        return 7;

    x = 1;
    if (!__sync_bool_compare_and_swap(&x, 1, 2) || x != 2)
        return 8;
    if (__sync_bool_compare_and_swap(&x, 1, 3) || x != 2)
        return 9;
    if (__sync_val_compare_and_swap(&x, 2, 7) != 2 || x != 7)
        return 10;
    if (__sync_val_compare_and_swap(&x, 2, 9) != 7 || x != 7)
        return 11;

    x = 5;
    if (__sync_lock_test_and_set(&x, 8) != 5 || x != 8)
        return 12;
    __sync_lock_release(&x);
    if (x != 0)
        return 13;
    __sync_synchronize();
    return 42;
}

static int sync_cell;
static int sync_calls;
static int *sync_target(void) {
    sync_calls++;
    return &sync_cell;
}
static int sync_value(void) {
    sync_calls++;
    return 2;
}

// The shapes src/dmon.h uses: a bool flag, a pointer pointee, and a trailing
// (discarded) protected-variable argument.
[[cccc::test(return = 42)]]
int test_sync_builtins_operand_shapes(void) {
    volatile bool quit = false;
    if (!__sync_bool_compare_and_swap(&quit, false, false))
        return 1;
    (void)__sync_lock_test_and_set(&quit, true);
    if (!quit)
        return 2;

    int  a = 1, b = 2;
    int *p = &a;
    if (!__sync_bool_compare_and_swap(&p, &a, &b) || p != &b)
        return 3;
    if (__sync_val_compare_and_swap(&p, &a, &a) != &b)
        return 4;

    long y = 0;
    if (__sync_fetch_and_add(&y, 1, &a, &b) != 0 || y != 1)
        return 5;

    // Operands evaluate exactly once.
    sync_calls = 0;
    __sync_fetch_and_add(sync_target(), sync_value());
    __sync_add_and_fetch(sync_target(), sync_value());
    __sync_bool_compare_and_swap(sync_target(), sync_value(), sync_value());
    if (sync_calls != 7 || sync_cell != 4)
        return 6;
    return 42;
}

[[cccc::test(return = 42)]]
int test_has_builtin_atomics(void) {
    if (!__has_builtin(__sync_fetch_and_add) ||
        !__has_builtin(__sync_bool_compare_and_swap) ||
        !__has_builtin(__atomic_load_n) ||
        !__has_builtin(__atomic_compare_exchange_n))
        return 1;
    return 42;
}

#pragma cccc suite end

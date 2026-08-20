// CCCC_FLAGS: --testing
// Consolidated suite: stack canaries, CFI, deep expressions, multi-VM
// isolation,
//   heap canaries, memory leak detection, memory poisoning, memory tagging,
//   thread safety runtime diagnostics
// Source tests: test_cfi_main_return, test_cfi_setjmp_shadow_stack,
//   test_deep_nested_expr_587, test_deep_nested_expr_587_safety,
//   test_multi_vm_isolation, test_stack_canary_float,
//   test_stack_canary_many_params, test_stack_canary_params,
//   test_stack_canary_threads,
//   test_heap_canary_clean, test_heap_canary_overflow,
//   test_memory_leak_detect_clean, test_memory_leak_detect_leak,
//   test_memory_poisoning_clean,
//   test_memory_tagging_basic, test_memory_tagging_multiple_gens,
//   test_memory_tagging_reuse,
//   test_thread_safety_double_lock, test_thread_safety_lock_order,
//   test_thread_safety_race, test_thread_safety_atomic_mix_runtime,
//   test_stack_overflow_large_frame, test_tagging_simple

#include <pthread.h>
#include <setjmp.h>

// [from test_deep_nested_expr_587]
// Regression test for ticket #587: deeply nested / right-leaning binary
// expression trees exhausted the fixed temp-register pool (11 regs, T0-T10).
// Ticket #295 fixed only the left spine; the right spine still grew O(depth)
// live temps. The codegen now spills the LHS to the stack under register
// pressure, bounding peak register use regardless of tree shape.

static int one(void) {
    return 1;
}

// [from test_deep_nested_expr_587_safety]
// Ticket #587 companion: exercise the LHS-spill binary-op path under checked
// arithmetic (-2). The spill path emits ops as `op rd, r_lhs, rd` where rd
// aliases the RHS operand; under -2 the add/sub/mul opcodes become the checked
// ADDC/SUBC/MULC variants, which the default-level test does not cover.

static int _deep_nested_expr_587_safety_one(void) {
    return 1;
}

// [from test_multi_vm_isolation]
// Test that multiple CCCC instances have isolated state
// This verifies the fix for global static variables moved to CCCC struct

// [from test_stack_canary_float]
// #445 — float/double params (the float_param_mask spill branch in ENT3) must
// land in the canary-shifted slots too.

static double fadd(double a, double b) {
    return a + b;
}

// Mixed int + float params with a local.

static float mix(int n, float f) {
    float local = (float)n + f;
    return local * 2.0f;
}

// [from test_stack_canary_many_params]
// #445 — more than 8 params exercises the stack-passed-arg branch in ENT3
// (bp[2 + (i-8)]); the register-spilled params must still land canary-shifted.

static int sum10(int a, int b, int c, int d, int e, int f, int g, int h, int i,
                 int j) {
    return a + b + c + d + e + f + g + h + i + j;
}

// [from test_stack_canary_params]
// #445 — functions with parameters must work under stack canaries.
// The canary lives at bp[-1]; params/locals must read from bp[-2] downward.

static int add(int a, int b) {
    return a + b;
}

// Multiple int params plus a local variable.

static int weighted(int a, int b, int c) {
    int local = a * 2;
    return local + b - c;
}

// [from test_stack_canary_threads]
// #445 — stack canaries must work alongside threading. Each thread runs
// functions with parameters; the canary-shifted frame layout must hold per
// thread without corrupting params/locals.

static int _stack_canary_threads_add(int a, int b) {
    int local = a;
    return local + b;
}

static void *worker(void *arg) {
    int n = (int)(long long)arg;
    // Exercise a few param-carrying calls inside the thread.
    long long acc = 0;
    for (int i = 0; i < 100; i++)
        acc += _stack_canary_threads_add(n, i);
    return (void *)acc;
}

// [from test_cfi_setjmp_shadow_stack]
// CFI regression: longjmp must restore shadow_sp so the caller of the setjmp
// function can return without consuming orphaned shadow entries.

static jmp_buf _cfi_env;

static void _cfi_level2(void) {
    longjmp(_cfi_env, 1);
}

static void _cfi_level1(void) {
    _cfi_level2();
}

// setjmp and longjmp happen inside this function; when it returns, shadow_sp
// must match what it was on entry, not be 2 levels deeper.
static int _cfi_do_work(void) {
    if (setjmp(_cfi_env) == 0)
        _cfi_level1();
    // After longjmp: shadow_sp has 2 orphaned entries from level1/level2 CALLs
    // unless SETJMP saved and LONGJMP restored shadow_sp.
    return 42;
}

#pragma cccc suite begin "stack_safety"

// test_cfi_main_return
[[cccc::test(return = 42)]]
int test_cfi_main_return(void) {
    return 42;
}

// test_cfi_setjmp_shadow_stack
[[cccc::test(return = 42, flags = "-C")]]
int test_cfi_setjmp_shadow_stack(void) {
    return _cfi_do_work();
}

// test_deep_nested_expr_587
[[cccc::test(return = 42)]]
int test_deep_nested_expr_587(void) {
    // Right-nested int addition, depth 40 (well past the old limit of 11).
    // 0 + 1 + 1 + ... (40 ones) = 40
    int ri =
        (1 +
         (1 +
          (1 +
           (1 +
            (1 +
             (1 +
              (1 +
               (1 +
                (1 +
                 (1 +
                  (1 +
                   (1 +
                    (1 +
                     (1 +
                      (1 +
                       (1 +
                        (1 +
                         (1 +
                          (1 +
                           (1 +
                            (1 +
                             (1 +
                              (1 +
                               (1 +
                                (1 +
                                 (1 +
                                  (1 +
                                   (1 +
                                    (1 +
                                     (1 +
                                      (1 +
                                       (1 +
                                        (1 +
                                         (1 +
                                          (1 +
                                           (1 +
                                            (1 +
                                             (1 +
                                              (1 +
                                               (1 +
                                                0))))))))))))))))))))))))))))))))))))))));
    if (ri != 40)
        return 1;

    // Right-nested subtraction with distinct operands — verifies that the
    // spill path preserves operand order for non-commutative ops.
    // 9-(8-(7-(6-(5-(4-(3-(2-(1-0)))))))) = 5
    int rs = (9 - (8 - (7 - (6 - (5 - (4 - (3 - (2 - (1 - 0)))))))));
    if (rs != 5)
        return 2;

    // Deep right-nested chain with a function call as the innermost operand —
    // exercises the spill path together with call-clobbered temp registers.
    // 0 + 1*15 + one() = 16
    int rc =
        (1 +
         (1 +
          (1 +
           (1 +
            (1 +
             (1 +
              (1 + (1 + (1 + (1 + (1 + (1 + (1 + (1 + (1 + one())))))))))))))));
    if (rc != 16)
        return 3;

    // Deep right-nested multiplication / division / unsigned comparison —
    // exercises the spill path for non-add opcodes (rd aliases the RHS
    // operand). 1*1*...*6 = 6 ; 1/1/.../1 = 1.
    int rm =
        (1 *
         (1 *
          (1 *
           (1 *
            (1 *
             (1 *
              (1 *
               (1 *
                (1 *
                 (1 *
                  (1 *
                   (1 *
                    (1 *
                     (1 * (1 * (1 * (1 * (1 * (1 * (1 * 6))))))))))))))))))));
    if (rm != 6)
        return 5;
    int rd2 =
        (1 /
         (1 /
          (1 /
           (1 /
            (1 /
             (1 /
              (1 /
               (1 /
                (1 /
                 (1 /
                  (1 /
                   (1 /
                    (1 /
                     (1 / (1 / (1 / (1 / (1 / (1 / (1 / 1))))))))))))))))))));
    if (rd2 != 1)
        return 6;
    // Right-nested unsigned compares collapse to 0 here; checks U-compare
    // opcode selection on the spill path.
    unsigned ru =
        (1u <
         (1u <
          (1u <
           (1u <
            (1u <
             (1u <
              (1u <
               (1u <
                (1u <
                 (1u <
                  (1u <
                   (1u <
                    (1u <
                     (1u <
                      (1u <
                       (1u < (1u < (1u < (1u < (1u < 2u))))))))))))))))))));
    if (ru != 0u)
        return 7;

    // Right-nested float addition, depth 40 — exercises the float spill path
    // (FR2R/R2FR bit moves around PSH3/POP3).
    float rf =
        (1.0f +
         (1.0f +
          (1.0f +
           (1.0f +
            (1.0f +
             (1.0f +
              (1.0f +
               (1.0f +
                (1.0f +
                 (1.0f +
                  (1.0f +
                   (1.0f +
                    (1.0f +
                     (1.0f +
                      (1.0f +
                       (1.0f +
                        (1.0f +
                         (1.0f +
                          (1.0f +
                           (1.0f +
                            (1.0f +
                             (1.0f +
                              (1.0f +
                               (1.0f +
                                (1.0f +
                                 (1.0f +
                                  (1.0f +
                                   (1.0f +
                                    (1.0f +
                                     (1.0f +
                                      (1.0f +
                                       (1.0f +
                                        (1.0f +
                                         (1.0f +
                                          (1.0f +
                                           (1.0f +
                                            (1.0f +
                                             (1.0f +
                                              (1.0f +
                                               (1.0f +
                                                0.0f))))))))))))))))))))))))))))))))))))))));
    if ((int)rf != 40)
        return 4;

    return 42;
}

// test_deep_nested_expr_587_safety
[[cccc::test(return = 42, flags = "-2")]]
int test_deep_nested_expr_587_safety(void) {
    // Deep right-nested signed add/sub → checked ADDC/SUBC on the spill path.
    int ra =
        (1 +
         (1 +
          (1 +
           (1 +
            (1 +
             (1 +
              (1 +
               (1 +
                (1 +
                 (1 +
                  (1 +
                   (1 +
                    (1 +
                     (1 + (1 + (1 + (1 + (1 + (1 + (1 + 0))))))))))))))))))));
    if (ra != 20)
        return 1;

    int rs = (9 - (8 - (7 - (6 - (5 - (4 - (3 - (2 - (1 - 0)))))))));
    if (rs != 5)
        return 2;

    // Deep nested with a call operand under -2 (checked arithmetic active).
    int rc =
        (1 +
         (1 +
          (1 +
           (1 +
            (1 +
             (1 +
              (1 +
               (1 +
                (1 +
                 (1 +
                  (1 +
                   (1 +
                    (1 +
                     (1 +
                      (1 + _deep_nested_expr_587_safety_one())))))))))))))));
    if (rc != 16)
        return 3;

    // Deep right-nested multiplication → checked MULC on the spill path.
    int rm =
        (1 *
         (1 *
          (1 *
           (1 *
            (1 *
             (1 *
              (1 *
               (1 *
                (1 *
                 (1 *
                  (1 *
                   (1 *
                    (1 *
                     (1 * (1 * (1 * (1 * (1 * (1 * (1 * 7))))))))))))))))))));
    if (rm != 7)
        return 4;

    return 42;
}

// test_multi_vm_isolation
[[cccc::test(return = 42)]]
int test_multi_vm_isolation(void) {
    // Test 1: __COUNTER__ isolation
    // Each compilation should have its own counter starting from 0
    int c1 = __COUNTER__; // Should be 0
    int c2 = __COUNTER__; // Should be 1
    int c3 = __COUNTER__; // Should be 2

    if (c1 != 0)
        return 1;
    if (c2 != 1)
        return 2;
    if (c3 != 2)
        return 3;

    // Test 2: Anonymous globals (uses unique_name_counter)
    // String literals create anonymous global variables with unique names
    char *s1 = "hello";
    char *s2 = "world";
    char *s3 = "test";

    // They should all be different pointers
    if (s1 == s2)
        return 4;
    if (s2 == s3)
        return 5;
    if (s1 == s3)
        return 6;

    // Success
    return 42;
}

// test_stack_canary_float
[[cccc::test(return = 42, flags = "--stack-canaries")]]
int test_stack_canary_float(void) {
    if (fadd(20.5, 21.5) != 42.0)
        return 1;
    if (mix(10, 1.0f) != 22.0f) // (10 + 1) * 2
        return 2;
    return 42;
}

// test_stack_canary_many_params
[[cccc::test(return = 42, flags = "--stack-canaries")]]
int test_stack_canary_many_params(void) {
    // 1+2+...+10 = 55
    if (sum10(1, 2, 3, 4, 5, 6, 7, 8, 9, 10) != 55)
        return 1;
    return 42;
}

// test_stack_canary_params
[[cccc::test(return = 42, flags = "--stack-canaries")]]
int test_stack_canary_params(void) {
    if (add(20, 22) != 42)
        return 1;
    if (weighted(10, 5, 7) != 18) // 20 + 5 - 7
        return 2;
    return 42;
}

// test_stack_canary_threads
[[cccc::test(return = 42, flags = "--stack-canaries")]]
int test_stack_canary_threads(void) {
    pthread_t a, b;
    if (pthread_create(&a, 0, worker, (void *)(long long)10) != 0)
        return 1;
    if (pthread_create(&b, 0, worker, (void *)(long long)20) != 0)
        return 2;
    void *ra = 0, *rb = 0;
    pthread_join(a, &ra);
    pthread_join(b, &rb);
    // worker(n) = sum_{i=0..99}(n + i) = 100*n + 4950
    if ((long long)ra != 100 * 10 + 4950)
        return 3;
    if ((long long)rb != 100 * 20 + 4950)
        return 4;
    // And a param-carrying call on the main thread too.
    if (_stack_canary_threads_add(20, 22) != 42)
        return 5;
    return 42;
}

// [from test_heap_canary_clean]
// Heap canary: clean alloc/free path should succeed.
#include <stdlib.h>
[[cccc::test(return = 42, flags = "--heap-canaries -V")]]
int test_heap_canary_clean(void) {
    int *p = malloc(sizeof(int) * 4);
    p[0]   = 1;
    p[1]   = 2;
    p[2]   = 3;
    p[3]   = 4;
    free(p);
    char *s = malloc(16);
    s[0]    = 'h';
    s[15]   = '\0';
    free(s);
    return 42;
}

// [from test_heap_canary_overflow]
// Heap canary: write past allocation corrupts rear canary → exit 255.
// Note: CCCC_EXPECT_STDERR not preserved (exit_code= tests use fork; stderr not
// captured).
[[cccc::test(exit_code = 255, flags = "--heap-canaries -V")]]
void test_heap_canary_overflow(void) {
    void *malloc(long);
    void free(void *);
    char *buf = (char *)malloc(8);
    buf[0]    = 'A';
    buf[8]    = 'X';
    buf[9]    = 'X';
    buf[10]   = 'X';
    buf[11]   = 'X';
    buf[12]   = 'X';
    buf[13]   = 'X';
    buf[14]   = 'X';
    buf[15]   = 'X';
    free(buf);
}

// [from test_memory_leak_detect_clean]
// Memory leak detection: all freed, no report.
[[cccc::test(return = 42, flags = "--memory-leak-detection -V")]]
int test_memory_leak_detect_clean(void) {
    int  *a = malloc(sizeof(int) * 4);
    char *b = malloc(32);
    a[0]    = 1;
    b[0]    = 'x';
    free(a);
    free(b);
    return 42;
}

// [from test_memory_leak_detect_leak]
// Memory leak detection: unfree'd malloc — leak report fires at VM shutdown,
// not during the test function, so expect_stderr cannot capture it.
// This verifies the program runs and exits 42 with leak detection enabled.
[[cccc::test(return = 42, flags = "--memory-leak-detection -V")]]
int test_memory_leak_detect_leak(void) {
    void *malloc(long);
    int *p = (int *)malloc(sizeof(int) * 8);
    p[0]   = 42;
    return 42;
}

// [from test_memory_poisoning_clean]
// Memory poisoning: clean malloc/memset/free should not crash.
#include <string.h>
[[cccc::test(return = 42, flags = "--memory-poisoning -V")]]
int test_memory_poisoning_clean(void) {
    char *p = malloc(64);
    memset(p, 0, 64);
    p[0] = 'z';
    free(p);
    int *q = malloc(sizeof(int) * 4);
    q[0]   = 1;
    q[1]   = 2;
    free(q);
    return 42;
}

// [from test_memory_tagging_basic]
// Memory tagging: UAF after realloc → TEMPORAL SAFETY VIOLATION → exit 255.
// Note: CCCC_EXPECT_STDERR not preserved (exit_code= uses fork).
[[cccc::test(exit_code = 255, flags = "--memory-tagging -V")]]
void test_memory_tagging_basic(void) {
    void *malloc(long);
    void free(void *);
    int *ptr1      = (int *)malloc(sizeof(int) * 10);
    *ptr1          = 42;
    int *stale_ptr = ptr1;
    free(ptr1);
    int *ptr2 = (int *)malloc(sizeof(int) * 10);
    *ptr2     = 100;
    int value = *stale_ptr; // stale generation → TEMPORAL SAFETY VIOLATION
    (void)value;
}

// [from test_memory_tagging_multiple_gens]
// Memory tagging: multiple alloc/free cycles, stale pointer access → exit 255.
[[cccc::test(exit_code = 255, flags = "--memory-tagging -V")]]
void test_memory_tagging_multiple_gens(void) {
    void *malloc(long);
    void free(void *);
    int *ptr1   = (int *)malloc(sizeof(int) * 10);
    *ptr1       = 10;
    int *stale1 = ptr1;
    free(ptr1);
    int *ptr2 = (int *)malloc(sizeof(int) * 10);
    *ptr2     = 20;
    free(ptr2);
    int *ptr3 = (int *)malloc(sizeof(int) * 10);
    *ptr3     = 30;
    free(ptr3);
    int *ptr4 = (int *)malloc(sizeof(int) * 10);
    *ptr4     = 40;
    free(ptr4);
    int bad_value = *stale1; // generation 0, current is higher → violation
    (void)bad_value;
}

// [from test_memory_tagging_reuse]
// Memory tagging: struct UAF via stale pointer → exit 255.
[[cccc::test(exit_code = 255, flags = "--memory-tagging -V")]]
void test_memory_tagging_reuse(void) {
    void *malloc(long);
    void free(void *);
    struct MemTagData {
        int value;
        int count;
    };
    struct MemTagData *data1 =
        (struct MemTagData *)malloc(sizeof(struct MemTagData));
    data1->value             = 42;
    data1->count             = 1;
    struct MemTagData *stale = data1;
    free(data1);
    struct MemTagData *data2 =
        (struct MemTagData *)malloc(sizeof(struct MemTagData));
    data2->value  = 100;
    data2->count  = 2;
    int bad_value = stale->value; // stale → TEMPORAL SAFETY VIOLATION
    (void)bad_value;
}

// [from test_thread_safety_double_lock]
// Thread safety: double-lock detection → DEADLOCK on stderr (exit 42).
[[cccc::test(return = 42, flags = "--thread-safety",
                    expect_stderr = "DEADLOCK")]]
int test_thread_safety_double_lock(void) {
    static pthread_mutex_t ts_m = PTHREAD_MUTEX_INITIALIZER;
    static void *ts_dlock_worker(void *arg) {
        (void)arg;
        pthread_mutex_lock(&ts_m);
        pthread_mutex_lock(&ts_m);
        pthread_mutex_unlock(&ts_m);
        pthread_mutex_unlock(&ts_m);
        return 0;
    }
    pthread_t t;
    if (pthread_create(&t, 0, ts_dlock_worker, 0) != 0)
        return 1;
    pthread_join(t, 0);
    pthread_mutex_destroy(&ts_m);
    return 42;
}

// [from test_thread_safety_lock_order]
// Thread safety: lock-order inversion → LOCK ORDER on stderr (exit 42).
[[cccc::test(return = 42, flags = "--thread-safety",
                    expect_stderr = "LOCK ORDER")]]
int test_thread_safety_lock_order(void) {
    static pthread_mutex_t ts_l1 = PTHREAD_MUTEX_INITIALIZER;
    static pthread_mutex_t ts_l2 = PTHREAD_MUTEX_INITIALIZER;
    static void *ts_thread_a(void *arg) {
        (void)arg;
        pthread_mutex_lock(&ts_l1);
        pthread_mutex_lock(&ts_l2);
        pthread_mutex_unlock(&ts_l2);
        pthread_mutex_unlock(&ts_l1);
        return 0;
    }
    static void *ts_thread_b(void *arg) {
        (void)arg;
        pthread_mutex_lock(&ts_l2);
        pthread_mutex_lock(&ts_l1);
        pthread_mutex_unlock(&ts_l1);
        pthread_mutex_unlock(&ts_l2);
        return 0;
    }
    pthread_t ta, tb;
    if (pthread_create(&ta, 0, ts_thread_a, 0) != 0)
        return 1;
    pthread_join(ta, 0);
    if (pthread_create(&tb, 0, ts_thread_b, 0) != 0)
        return 2;
    pthread_join(tb, 0);
    pthread_mutex_destroy(&ts_l1);
    pthread_mutex_destroy(&ts_l2);
    return 42;
}

// [from test_thread_safety_race]
// Thread safety: unsynchronized shared-counter race → DATA RACE DETECTED (exit
// 42).
[[cccc::test(return = 42, flags = "--thread-safety",
                    expect_stderr = "DATA RACE DETECTED")]]
int test_thread_safety_race(void) {
    static int *ts_shared;
    static void *ts_inc(void *arg) {
        (void)arg;
        *ts_shared = *ts_shared + 1;
        return 0;
    }
    int x     = 0;
    ts_shared = &x;
    pthread_t a, b;
    if (pthread_create(&a, 0, ts_inc, 0) != 0)
        return 1;
    if (pthread_create(&b, 0, ts_inc, 0) != 0)
        return 2;
    pthread_join(a, 0);
    pthread_join(b, 0);
    return 42;
}

// [from test_thread_safety_atomic_mix_runtime]
// Thread safety: mixed atomic/non-atomic access → MIXED ATOMIC/NON-ATOMIC (exit
// 42).
#include <stdatomic.h>
[[cccc::test(return = 42, flags = "--thread-safety",
                    expect_stderr = "MIXED ATOMIC/NON-ATOMIC ACCESS DETECTED")]]
int test_thread_safety_atomic_mix_runtime(void) {
    static _Atomic int ts_shared_at = 0;
    static void *ts_plain_read(void *arg) {
        (void)arg;
        int *p = (int *)&ts_shared_at;
        int  v = *p;
        return (void *)(long long)v;
    }
    atomic_store(&ts_shared_at, 1);
    pthread_t t;
    if (pthread_create(&t, 0, ts_plain_read, 0) != 0)
        return 1;
    pthread_join(t, 0);
    return 42;
}

// [from test_stack_overflow_large_frame]
// Large stack frame (500 000 longs) should trigger stack overflow detection.
[[cccc::test(expect_runtime_error = true)]]
int test_stack_overflow_large_frame(void) {
    long long arr[500000];
    arr[0] = 42;
    return (int)arr[0];
}

// [from test_tagging_simple]
// Simple malloc/free returning 42 — baseline for memory-tagging infrastructure.
[[cccc::test(return = 42)]]
int test_tagging_simple(void) {
    int *ptr   = (int *)malloc(sizeof(int));
    *ptr       = 42;
    int result = *ptr;
    free(ptr);
    return result;
}

#pragma cccc suite end

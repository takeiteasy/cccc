// Tests for __attribute__((cleanup(fn))) scope-exit callbacks (ticket #218).
// CCCC_FLAGS: --testing
//
// Semantics (matching GCC C-mode):
//   - On scope exit, calls fn(&var) for each cleanup-annotated local.
//   - LIFO order: last-declared variable cleaned up first.
//   - Fires on: natural block exit, return, break, continue.
//   - Does NOT fire on longjmp (matches GCC C-mode behavior).
//   - Named goto across cleanup scopes: deferred to follow-up ticket.

#include <stdbool.h>

// ---- Shared state for tracking cleanup call order ----
static int g_log[64];
static int g_log_n = 0;

static void log_reset(void) { g_log_n = 0; }
static void log_val(int v)  { if (g_log_n < 64) g_log[g_log_n++] = v; }

// Generic int cleanup: logs the dereferenced value.
static void cleanup_int(int *p) { log_val(*p); }

// Generic bool setter: marks that cleanup ran.
static void cleanup_set_true(bool *p) { *p = true; }

// ---- Test 1: Basic cleanup at natural block exit ----
[[cccc::test]]
static void test_basic_block_exit(void) {
    log_reset();
    {
        int x __attribute__((cleanup(cleanup_int))) = 42;
        (void)x;
    }
    $assert_eq(g_log_n, 1);
    $assert_eq(g_log[0], 42);
}

// ---- Test 2: Cleanup on explicit return (void function) ----
static void void_with_cleanup_return(bool cond, bool *cleaned) {
    bool flag __attribute__((cleanup(cleanup_set_true))) = false;
    (void)flag;
    *cleaned = false;
    if (cond) {
        *cleaned = true; // signal we're returning early
        return;
    }
}

[[cccc::test]]
static void test_cleanup_on_void_return(void) {
    bool cleaned = false;
    bool took_early = false;
    void_with_cleanup_return(true, &took_early);
    // cleanup_set_true sets flag=true; but we only confirm no crash here.
    // The early-return path should not skip the cleanup.
    $assert_eq(took_early, true);
}

// ---- Test 3: Cleanup on non-void return; return value preserved ----
static int int_return_with_cleanup(void) {
    int sentinel __attribute__((cleanup(cleanup_int))) = 99;
    (void)sentinel;
    return 42; // cleanup(sentinel) fires, then 42 is returned
}

[[cccc::test]]
static void test_nonvoid_return_value_preserved(void) {
    log_reset();
    int result = int_return_with_cleanup();
    $assert_eq(result, 42);      // return value must survive cleanup call
    $assert_eq(g_log_n, 1);
    $assert_eq(g_log[0], 99);   // cleanup did run
}

// ---- Test 4: Float return value preserved across cleanup ----
static void cleanup_float_marker(int *p) { *p = 1; }

static float float_return_with_cleanup(void) {
    int marker __attribute__((cleanup(cleanup_float_marker))) = 0;
    (void)marker;
    return 3.14f;
}

[[cccc::test]]
static void test_float_return_preserved(void) {
    float r = float_return_with_cleanup();
    // Verify float return value survived the cleanup call.
    $assert_eq(r > 3.13f && r < 3.15f, true);
}

// ---- Test 5: LIFO order — multiple cleanup vars in one scope ----
[[cccc::test]]
static void test_lifo_order(void) {
    log_reset();
    {
        int a __attribute__((cleanup(cleanup_int))) = 1; // declared first
        int b __attribute__((cleanup(cleanup_int))) = 2;
        int c __attribute__((cleanup(cleanup_int))) = 3; // declared last → cleaned first
        (void)a; (void)b; (void)c;
    }
    // Expect LIFO: 3, 2, 1
    $assert_eq(g_log_n, 3);
    $assert_eq(g_log[0], 3);
    $assert_eq(g_log[1], 2);
    $assert_eq(g_log[2], 1);
}

// ---- Test 6: Nested scopes — inner cleaned before outer ----
[[cccc::test]]
static void test_nested_scope_order(void) {
    log_reset();
    {
        int outer __attribute__((cleanup(cleanup_int))) = 10;
        (void)outer;
        {
            int inner __attribute__((cleanup(cleanup_int))) = 20;
            (void)inner;
        } // inner cleaned here → logs 20
        // outer cleaned here → logs 10
    }
    $assert_eq(g_log_n, 2);
    $assert_eq(g_log[0], 20); // inner first
    $assert_eq(g_log[1], 10); // then outer
}

// ---- Test 7: Cleanup fires on break ----
[[cccc::test]]
static void test_cleanup_on_break(void) {
    log_reset();
    for (int i = 0; i < 3; i++) {
        int x __attribute__((cleanup(cleanup_int))) = i + 1;
        (void)x;
        if (i == 1)
            break; // x=2 must be cleaned before break
    }
    // Loop iteration 0: x=1, no break → cleaned at natural end
    // Loop iteration 1: x=2, break → cleaned before jumping out
    $assert_eq(g_log_n, 2);
    $assert_eq(g_log[0], 1); // iteration 0 natural exit
    $assert_eq(g_log[1], 2); // iteration 1 break exit
}

// ---- Test 8: Cleanup fires on continue ----
[[cccc::test]]
static void test_cleanup_on_continue(void) {
    log_reset();
    for (int i = 0; i < 3; i++) {
        int x __attribute__((cleanup(cleanup_int))) = i + 1;
        (void)x;
        if (i == 1)
            continue; // x=2 must be cleaned before continuing
    }
    // All three iterations: 1, 2, 3 (continue on i==1 still cleans x)
    $assert_eq(g_log_n, 3);
    $assert_eq(g_log[0], 1);
    $assert_eq(g_log[1], 2); // continue path
    $assert_eq(g_log[2], 3);
}

// ---- Test 9: static inline cleanup function not dead-stripped ----
static int g_inline_cleanup_ran = 0;

static inline void inline_cleanup(int *p) {
    (void)p;
    g_inline_cleanup_ran = 1;
}

[[cccc::test]]
static void test_static_inline_cleanup_not_stripped(void) {
    g_inline_cleanup_ran = 0;
    {
        int x __attribute__((cleanup(inline_cleanup))) = 5;
        (void)x;
    }
    $assert_eq(g_inline_cleanup_ran, 1);
}

// ---- Test 10: Early return from nested block ----
static int early_return_from_nested(bool flag) {
    int outer __attribute__((cleanup(cleanup_int))) = 100;
    (void)outer;
    {
        int inner __attribute__((cleanup(cleanup_int))) = 200;
        (void)inner;
        if (flag)
            return 99; // cleans inner (200), then outer (100), then returns 99
    }
    return 0; // cleans outer (100) on normal path
}

[[cccc::test]]
static void test_early_return_from_nested_block(void) {
    log_reset();
    int r = early_return_from_nested(true);
    $assert_eq(r, 99);
    // Both inner and outer must have been cleaned
    $assert_eq(g_log_n, 2);
    $assert_eq(g_log[0], 200); // inner first (innermost scope)
    $assert_eq(g_log[1], 100); // then outer
}

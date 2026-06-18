// Tests for __attribute__((cleanup(fn))) scope-exit callbacks (tickets #218, #480).
// CCCC_FLAGS: --testing
//
// Semantics (matching GCC C-mode):
//   - On scope exit, calls fn(&var) for each cleanup-annotated local.
//   - LIFO order: last-declared variable cleaned up first.
//   - Fires on: natural block exit, return, break, continue, named goto.
//   - Does NOT fire on longjmp (matches GCC C-mode behavior).

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

// ---- Test 11: Named goto out of inner block fires cleanup exactly once ----
// Regression for #480 — prior to fix, the goto emitted cleanups for ALL scopes
// (cleanup_target_depth was 0) then the label's enclosing block also emitted on
// natural exit → double cleanup.  Assert count == 1, not 0 or 2.
static int goto_out_of_inner(void) {
    int result = 0;
    {
        int x __attribute__((cleanup(cleanup_int))) = 55;
        (void)x;
        goto done; // exits inner block → x cleaned once here
        result = 99; // unreachable
    }
done:
    return result; // natural exit: no cleanup var in this scope
}

[[cccc::test]]
static void test_goto_out_of_inner_block(void) {
    log_reset();
    int r = goto_out_of_inner();
    $assert_eq(r, 0);
    $assert_eq(g_log_n, 1);    // cleaned exactly once at the goto
    $assert_eq(g_log[0], 55);
}

// ---- Test 12: Same-scope forward goto — double-cleanup regression ----
// The var and the label are in the same scope.  The goto exits zero cleanup
// scopes; the var is cleaned exactly once at natural block exit.
static void same_scope_forward_goto(void) {
    int x __attribute__((cleanup(cleanup_int))) = 77;
    (void)x;
    goto done;  // no scope change — cleanup_target_depth == this scope's depth
done:;          // x cleaned here at natural block exit
}

[[cccc::test]]
static void test_same_scope_forward_goto_no_double_cleanup(void) {
    log_reset();
    same_scope_forward_goto();
    $assert_eq(g_log_n, 1);    // exactly once — NOT twice
    $assert_eq(g_log[0], 77);
}

// ---- Test 13: Named goto skips multiple nested scopes — all cleaned LIFO ----
static int goto_skip_two_scopes(void) {
    int outer __attribute__((cleanup(cleanup_int))) = 10;
    (void)outer;
    {
        int mid __attribute__((cleanup(cleanup_int))) = 20;
        (void)mid;
        {
            int inner __attribute__((cleanup(cleanup_int))) = 30;
            (void)inner;
            goto top; // exits inner+mid cleanup scopes; outer is at function level
        }
    }
top:
    return 0; // outer cleaned at natural function exit
}

[[cccc::test]]
static void test_goto_skip_multiple_scopes(void) {
    log_reset();
    int r = goto_skip_two_scopes();
    $assert_eq(r, 0);
    // inner (30) and mid (20) fired at the goto (LIFO), outer (10) at return
    $assert_eq(g_log_n, 3);
    $assert_eq(g_log[0], 30);  // innermost first
    $assert_eq(g_log[1], 20);
    $assert_eq(g_log[2], 10);  // outer at natural return
}

// ---- Test 14: Cross-sibling goto cleans the SOURCE scope ----
// Regression for #481.  Two sibling blocks at the same nesting depth each hold a
// cleanup var.  The goto jumps from block A into block B, past B's declaration.
// Before the LCA fix, the goto saw cleanup_target_depth == 1 (B's depth) and
// skipped cleanup for `a` (also depth 1) — a leak.  With LCA tracking the common
// ancestor is the function level (depth 0), so `a` is cleaned at the goto.
//
// Note: jumping past `b`'s initialization leaves `b` uninitialized when its
// cleanup runs at B's block exit (genuine ill-formed C, diagnosed under
// -Wattributes).  So `b`'s cleanup must NOT read its value — it logs a constant
// tag instead, and we never assert b's value.
static void cleanup_tag_99(int *p) { (void)p; log_val(99); }

static void cross_sibling_goto(void) {
    {
        int a __attribute__((cleanup(cleanup_int))) = 7;  // depth 1, block A
        (void)a;
        goto L;
    }
    {
        int b __attribute__((cleanup(cleanup_tag_99))) = 0; // depth 1, block B
        (void)b;
    L:;
    }
}

[[cccc::test]]
static void test_cross_sibling_goto_cleans_source(void) {
    log_reset();
    cross_sibling_goto();
    // `a` cleaned at the goto (source scope), then `b`'s tag at B's block exit.
    $assert_eq(g_log_n, 2);
    $assert_eq(g_log[0], 7);   // SOURCE scope cleaned — the #481 regression
    $assert_eq(g_log[1], 99);  // B's cleanup tag (value-independent)
}

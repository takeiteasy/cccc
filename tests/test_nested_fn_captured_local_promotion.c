// CCCC_FLAGS: --testing -O2
// Regression coverage for #836: a local captured by a GNU nested function
// (reached through the static-link chain and that local's own stack slot,
// with no explicit `&` anywhere) was never marked `is_captured`. That flag is
// only ever set by collect_captures_in_node, and that function only runs for
// Apple block literals (^{ ... }), not for plain GNU nested functions. At
// `--optimize=2`+, prepare_local_promotion / prepare_fp_local_promotion
// (src/codegen.c) promote hot scalar/FP locals into saved VM registers
// (S0-S3 / FREG_S0-S3) unless is_captured is set -- so an unmarked captured
// local got held in a register by the enclosing function while the nested
// function kept reading/writing its stack slot behind that register's back.
// Both directions (nested fn writes, parent reads stale register value; or
// parent writes, nested fn reads a stale stack slot) were silently wrong.
//
// Each test below deliberately pads the parent-scope local with enough uses
// to clear collect_promotion_candidates' score >= 3 gate, so promotion
// actually fires and the test is not vacuous. Confirmed to fail (wrong
// value) on the pre-fix binary at -O2 for every case here.

#include <stddef.h>

// ---------------------------------------------------------------------------
// Write direction: nested function assigns an enclosing int local; the
// parent must observe the write after the nested function returns.
// ---------------------------------------------------------------------------

[[cccc::test]]
void test_nested_fn_write_int_visible_to_parent(void) {
    int s = 1;
    s = s + 1; // pad score: several parent-scope uses of s
    s = s + 1;
    s = s + 1;
    void set_to_42(void) { s = 42; }
    set_to_42();
    AssertEq(s, 42);
}

// ---------------------------------------------------------------------------
// Read direction: parent mutates after the nested function is defined; the
// nested function must observe the current value, not a stale register copy
// or a stale initializer.
// ---------------------------------------------------------------------------

[[cccc::test]]
void test_nested_fn_read_int_sees_parent_write(void) {
    int s = 0;
    int seen = 0;
    void capture(void) { seen = s; }
    s = 1;
    s = s + 1;
    s = s + 1;
    s = 7;
    capture();
    AssertEq(seen, 7);
}

// ---------------------------------------------------------------------------
// FP local: exercises prepare_fp_local_promotion / is_fp_promotion_candidate_ok,
// the float-register counterpart of the same bug.
// ---------------------------------------------------------------------------

[[cccc::test]]
void test_nested_fn_write_double_visible_to_parent(void) {
    double d = 1.0;
    d = d + 1.0;
    d = d + 1.0;
    d = d + 1.0;
    void set_to_half(void) { d = 0.5; }
    set_to_half();
    AssertEq((int)(d * 2.0), 1);
}

// ---------------------------------------------------------------------------
// Two-level nest: main -> mid -> inner, where inner writes a main-scope local
// with no direct nesting relationship to main. The fix must be depth-agnostic
// (any is_local ND_VAR not in the current function's own locals list belongs
// to *some* enclosing frame), not limited to the immediate parent's locals.
// ---------------------------------------------------------------------------

[[cccc::test]]
void test_nested_fn_two_level_write_visible_to_outer(void) {
    int outer = 1;
    outer = outer + 1;
    outer = outer + 1;
    outer = outer + 1;
    void inner(void) { outer = 99; }
    void mid(void) { inner(); }
    mid();
    AssertEq(outer, 99);
}

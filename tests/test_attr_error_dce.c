// CCCC_FLAGS: --testing
// DCE-aware __attribute__((error)) suppression (#637, #644).
//
// All calls to __chk_fail() below sit in statically-dead branches; none should
// trigger a compile-time error.  The test verifies:
//   - plain constant-false if  (const-fold tier)
//   - fixed buffer + constant length comparison  (const-fold tier)
//   - runtime-length / unknown-pointer FORTIFY wrapper  (unsigned tautology tier)
//   - nested dead branches  (counter nesting)
//   - dead else-branch  (always-true condition)
//   - while(0) dead loop body  (#644)
//   - for(;0;) dead loop body  (#644)
//   - false && chk() short-circuit  (#644)
//   - true || chk() short-circuit  (#644)
//   - ternary dead then-branch  (#644)
//   - ternary dead else-branch  (#644)
//
// NOTE on types: __builtin_object_size returns ty_ulong (unsigned long).
// size_t is typedef unsigned long (#643), so the FORTIFY idiom
// `len > __builtin_object_size(ptr, 0)` is a native unsigned-vs-unsigned
// comparison.  The conservative return value SIZE_MAX (= (unsigned long)-1
// = UINT64_MAX) means `len > UINT64_MAX` is always false for any valid
// length, making the branch statically dead.
//
// Live calls in a live branch still error; that is covered by the existing
// test_attr_error.c and test_attr_error_live*.c (EXPECT_COMPILE_ERROR).

#include <stddef.h>

void __chk_fail(void) __attribute__((error("buffer overflow detected")));
void __chk_fail(void) { /* body needed for link; never called at runtime (dead branches) */ }
void __chk_warn(void) __attribute__((warning("potential overflow")));
void __chk_warn(void) { /* body: in real use this would abort */ }

// int-returning variants for value-context positions (&&, ||, ternary)
// where void operands are a type error.
int __chk_fail_i(void) __attribute__((error("buffer overflow detected")));
int __chk_fail_i(void) { return 0; }
int __chk_warn_i(void) __attribute__((warning("potential overflow")));
int __chk_warn_i(void) { return 0; }

// --- Tier 1: plain constant fold ---

[[cccc::test]]
void test_error_const_false_if(void) {
    // if (0) is statically dead; __chk_fail() must not trigger.
    if (0) __chk_fail();
}

[[cccc::test]]
void test_error_const_true_else(void) {
    // if (1): then is live, else is dead.
    if (1) { /* live */ } else { __chk_fail(); }
}

[[cccc::test]]
void test_error_fixed_buffer_overflow_check(void) {
    // Mirrors the FORTIFY idiom with a statically-known buffer.
    // 10 > __builtin_object_size(buf, 0) == 10 > 64 == false → dead branch.
    // (If len were 100, 100 > 64 == true and __chk_fail would correctly fire.)
    char buf[64];
    if (10 > __builtin_object_size(buf, 0)) __chk_fail();
}

[[cccc::test]]
void test_error_nested_dead_branches(void) {
    // Nesting: outer dead branch contains an inner live-looking call.
    // dead_code_depth must be > 0 for both levels.
    if (0) {
        if (1) { __chk_fail(); }  // still inside outer dead block
    }
}

// --- Tier 2: unsigned tautology fold ---

// FORTIFY-style wrapper: dst and len are runtime values.  __builtin_object_size
// returns unsigned long SIZE_MAX for an unknown/parameter pointer (type 0 fallback).
// The condition becomes: (unsigned long)len > UINT64_MAX — always false, so the
// branch is statically dead and the error is suppressed.
static void memcpy_chk(char *dst, const char *src, size_t len) {
    if (len > __builtin_object_size(dst, 0))
        __chk_fail();
    // (actual copy omitted — this test only checks compilation)
    (void)src;
}

[[cccc::test]]
void test_error_unsigned_tautology_fortify_wrapper(void) {
    char buf[32];
    memcpy_chk(buf, "hello", 5);
}

// --- Warning variant: suppressed in dead branch, still fires in live branch ---

[[cccc::test]]
void test_warning_suppressed_in_dead_branch(void) {
    if (0) __chk_warn();
}

[[cccc::test]]
void test_warning_unsigned_tautology_suppressed(void) {
    char *p = 0;
    size_t len = 5;
    if (len > __builtin_object_size(p, 0))
        __chk_warn();  // len > UINT64_MAX → always false → suppressed
}

// --- #644: while(0) and for(;0;) dead loop bodies ---

[[cccc::test]]
void test_error_while_zero(void) {
    // while(0) body is statically dead; error must be suppressed.
    while (0) __chk_fail();
}

[[cccc::test]]
void test_error_for_zero_cond(void) {
    // for(;0;) body is statically dead; error must be suppressed.
    for (; 0;) __chk_fail();
}

[[cccc::test]]
void test_warning_while_zero(void) {
    while (0) __chk_warn();
}

[[cccc::test]]
void test_warning_for_zero_cond(void) {
    for (; 0;) __chk_warn();
}

// --- #644: logical &&/|| short-circuit dead operand ---

[[cccc::test]]
void test_error_logand_false_lhs(void) {
    // 0 && chk(): RHS is never evaluated — statically dead.
    (void)(0 && __chk_fail_i());
}

[[cccc::test]]
void test_error_logor_true_lhs(void) {
    // 1 || chk(): RHS is never evaluated — statically dead.
    (void)(1 || __chk_fail_i());
}

[[cccc::test]]
void test_warning_logand_false_lhs(void) {
    (void)(0 && __chk_warn_i());
}

[[cccc::test]]
void test_warning_logor_true_lhs(void) {
    (void)(1 || __chk_warn_i());
}

// --- #644: ternary ?: dead branch ---

[[cccc::test]]
void test_error_ternary_dead_then(void) {
    // 0 ? dead : live — then branch is statically dead.
    (void)(0 ? __chk_fail_i() : 0);
}

[[cccc::test]]
void test_error_ternary_dead_else(void) {
    // 1 ? live : dead — else branch is statically dead.
    (void)(1 ? 0 : __chk_fail_i());
}

[[cccc::test]]
void test_warning_ternary_dead_then(void) {
    (void)(0 ? __chk_warn_i() : 0);
}

[[cccc::test]]
void test_warning_ternary_dead_else(void) {
    (void)(1 ? 0 : __chk_warn_i());
}

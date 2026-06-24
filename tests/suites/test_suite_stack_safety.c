// CCCC_FLAGS: --testing
// Consolidated suite: stack canaries, CFI, deep expressions, multi-VM isolation
// Source tests: test_cfi_main_return, test_deep_nested_expr_587, test_deep_nested_expr_587_safety, test_multi_vm_isolation, test_stack_canary_float, test_stack_canary_many_params, test_stack_canary_params, test_stack_canary_threads

#include <pthread.h>

// [from test_deep_nested_expr_587]
// Regression test for ticket #587: deeply nested / right-leaning binary
// expression trees exhausted the fixed temp-register pool (11 regs, T0-T10).
// Ticket #295 fixed only the left spine; the right spine still grew O(depth)
// live temps. The codegen now spills the LHS to the stack under register
// pressure, bounding peak register use regardless of tree shape.

static int one(void) { return 1; }

// [from test_deep_nested_expr_587_safety]
// Ticket #587 companion: exercise the LHS-spill binary-op path under checked
// arithmetic (-2). The spill path emits ops as `op rd, r_lhs, rd` where rd
// aliases the RHS operand; under -2 the add/sub/mul opcodes become the checked
// ADDC/SUBC/MULC variants, which the default-level test does not cover.

static int _deep_nested_expr_587_safety_one(void) { return 1; }

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

static int sum10(int a, int b, int c, int d, int e,
          int f, int g, int h, int i, int j) {
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

#pragma cccc suite begin "stack_safety"

// test_cfi_main_return
[[cccc::test(return = 42)]]
int test_cfi_main_return(void) {
    return 42;
}

// test_deep_nested_expr_587
[[cccc::test(return = 42)]]
int test_deep_nested_expr_587(void) {
    // Right-nested int addition, depth 40 (well past the old limit of 11).
    // 0 + 1 + 1 + ... (40 ones) = 40
    int ri =
        (1+(1+(1+(1+(1+(1+(1+(1+(1+(1+
        (1+(1+(1+(1+(1+(1+(1+(1+(1+(1+
        (1+(1+(1+(1+(1+(1+(1+(1+(1+(1+
        (1+(1+(1+(1+(1+(1+(1+(1+(1+(1+
        0))))))))))))))))))))))))))))))))))))))));
    if (ri != 40) return 1;

    // Right-nested subtraction with distinct operands — verifies that the
    // spill path preserves operand order for non-commutative ops.
    // 9-(8-(7-(6-(5-(4-(3-(2-(1-0)))))))) = 5
    int rs = (9-(8-(7-(6-(5-(4-(3-(2-(1-0)))))))));
    if (rs != 5) return 2;

    // Deep right-nested chain with a function call as the innermost operand —
    // exercises the spill path together with call-clobbered temp registers.
    // 0 + 1*15 + one() = 16
    int rc =
        (1+(1+(1+(1+(1+(1+(1+(1+(1+(1+
        (1+(1+(1+(1+(1+ one())))))))))))))));
    if (rc != 16) return 3;

    // Deep right-nested multiplication / division / unsigned comparison —
    // exercises the spill path for non-add opcodes (rd aliases the RHS operand).
    // 1*1*...*6 = 6 ; 1/1/.../1 = 1.
    int rm =
        (1*(1*(1*(1*(1*(1*(1*(1*(1*(1*
        (1*(1*(1*(1*(1*(1*(1*(1*(1*(1*6))))))))))))))))))));
    if (rm != 6) return 5;
    int rd2 =
        (1/(1/(1/(1/(1/(1/(1/(1/(1/(1/
        (1/(1/(1/(1/(1/(1/(1/(1/(1/(1/1))))))))))))))))))));
    if (rd2 != 1) return 6;
    // Right-nested unsigned compares collapse to 0 here; checks U-compare
    // opcode selection on the spill path.
    unsigned ru =
        (1u<(1u<(1u<(1u<(1u<(1u<(1u<(1u<(1u<(1u<
        (1u<(1u<(1u<(1u<(1u<(1u<(1u<(1u<(1u<(1u<2u))))))))))))))))))));
    if (ru != 0u) return 7;

    // Right-nested float addition, depth 40 — exercises the float spill path
    // (FR2R/R2FR bit moves around PSH3/POP3).
    float rf =
        (1.0f+(1.0f+(1.0f+(1.0f+(1.0f+(1.0f+(1.0f+(1.0f+(1.0f+(1.0f+
        (1.0f+(1.0f+(1.0f+(1.0f+(1.0f+(1.0f+(1.0f+(1.0f+(1.0f+(1.0f+
        (1.0f+(1.0f+(1.0f+(1.0f+(1.0f+(1.0f+(1.0f+(1.0f+(1.0f+(1.0f+
        (1.0f+(1.0f+(1.0f+(1.0f+(1.0f+(1.0f+(1.0f+(1.0f+(1.0f+(1.0f+
        0.0f))))))))))))))))))))))))))))))))))))))));
    if ((int)rf != 40) return 4;

    return 42;
}

// test_deep_nested_expr_587_safety
[[cccc::test(return = 42, flags = "-2")]]
int test_deep_nested_expr_587_safety(void) {
    // Deep right-nested signed add/sub → checked ADDC/SUBC on the spill path.
    int ra =
        (1+(1+(1+(1+(1+(1+(1+(1+(1+(1+
        (1+(1+(1+(1+(1+(1+(1+(1+(1+(1+
        0))))))))))))))))))));
    if (ra != 20) return 1;

    int rs = (9-(8-(7-(6-(5-(4-(3-(2-(1-0)))))))));
    if (rs != 5) return 2;

    // Deep nested with a call operand under -2 (checked arithmetic active).
    int rc =
        (1+(1+(1+(1+(1+(1+(1+(1+(1+(1+
        (1+(1+(1+(1+(1+ _deep_nested_expr_587_safety_one())))))))))))))));
    if (rc != 16) return 3;

    // Deep right-nested multiplication → checked MULC on the spill path.
    int rm =
        (1*(1*(1*(1*(1*(1*(1*(1*(1*(1*
        (1*(1*(1*(1*(1*(1*(1*(1*(1*(1*7))))))))))))))))))));
    if (rm != 7) return 4;

    return 42;
}

// test_multi_vm_isolation
[[cccc::test(return = 42)]]
int test_multi_vm_isolation(void) {
    // Test 1: __COUNTER__ isolation
    // Each compilation should have its own counter starting from 0
    int c1 = __COUNTER__;  // Should be 0
    int c2 = __COUNTER__;  // Should be 1
    int c3 = __COUNTER__;  // Should be 2

    if (c1 != 0) return 1;
    if (c2 != 1) return 2;
    if (c3 != 2) return 3;

    // Test 2: Anonymous globals (uses unique_name_counter)
    // String literals create anonymous global variables with unique names
    char *s1 = "hello";
    char *s2 = "world";
    char *s3 = "test";

    // They should all be different pointers
    if (s1 == s2) return 4;
    if (s2 == s3) return 5;
    if (s1 == s3) return 6;

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

#pragma cccc suite end

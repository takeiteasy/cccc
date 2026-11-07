// CCCC_FLAGS: --stack-canaries --testing
// #445 / #606 — va_start must account for the reserved canary slot when
// counting register-spill slots (__CCCC_STACK_CANARIES__ correction in
// stdarg.h).
//
// This test lives in its own suite file compiled with --stack-canaries at
// FILE level rather than as a per-test flag.  Per-test flags only affect
// codegen from the already-parsed AST; __CCCC_STACK_CANARIES__ is a
// preprocessor macro expanded at parse time, so a per-test lazy-recompile
// cannot update the literal already baked into the va_start expansion (#606).
// Source test: test_stack_canary_variadic.c

#include <stdarg.h>

static int sum(int count, ...) {
    va_list ap;
    va_start(ap, count);
    int total = 0;
    for (int i = 0; i < count; i++)
        total += va_arg(ap, int);
    va_end(ap);
    return total;
}

// C23 one-argument va_start: the named-parameter count, not &last, locates
// the tail, so it must also account for the canary slot.
static int sum_unnamed(...) {
    va_list ap;
    va_start(ap);
    int total = 0;
    for (int i = 0; i < 10; i++)
        total += va_arg(ap, int);
    va_end(ap);
    return total;
}

static int sum_one_arg(int count, ...) {
    va_list ap;
    va_start(ap);
    int total = 0;
    for (int i = 0; i < count; i++)
        total += va_arg(ap, int);
    va_end(ap);
    return total;
}

#pragma cccc suite begin "canary_variadic"

// test_stack_canary_variadic
[[cccc::test(return = 42)]]
int test_stack_canary_variadic(void) {
    if (sum(3, 10, 20, 12) != 42)
        return 1;
    // Enough varargs to spill past the register area into the caller stack
    // area.
    if (sum(10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10) != 55)
        return 2;
    return 42;
}

[[cccc::test(return = 42)]]
int test_canary_va_start_one_arg(void) {
    if (sum_unnamed(1, 2, 3, 4, 5, 6, 7, 8, 9, 10) != 55)
        return 1;
    if (sum_one_arg(3, 10, 20, 12) != 42)
        return 2;
    // Past the register area into the caller's stack.
    if (sum_one_arg(10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10) != 55)
        return 3;
    return 42;
}

#pragma cccc suite end

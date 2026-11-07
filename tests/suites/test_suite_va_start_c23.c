// CCCC_FLAGS: --std=c23 --testing
// C23 one-argument va_start(ap): the variadic tail is found from the named
// parameter count, so `int f(...)` and an omitted `last` both work.

#include <stdarg.h>

static int sum_all(...) {
    va_list ap;
    va_start(ap);
    int total = 0;
    for (int i = 0; i < 4; i++)
        total += va_arg(ap, int);
    va_end(ap);
    return total;
}

static int sum_n(int n, ...) {
    va_list ap;
    va_start(ap);
    int total = 0;
    for (int i = 0; i < n; i++)
        total += va_arg(ap, int);
    va_end(ap);
    return total;
}

static int sum_n_two_arg(int n, ...) {
    va_list ap;
    va_start(ap, n);
    int total = 0;
    for (int i = 0; i < n; i++)
        total += va_arg(ap, int);
    va_end(ap);
    return total;
}

static double mixed(...) {
    va_list ap;
    va_start(ap);
    int    a = va_arg(ap, int);
    double b = va_arg(ap, double);
    long   c = va_arg(ap, long);
    va_end(ap);
    return a + b + c;
}

static int nine_named(int a, int b, int c, int d, int e, int f, int g, int h,
                      int i, ...) {
    va_list ap;
    va_start(ap);
    int x = va_arg(ap, int);
    int y = va_arg(ap, int);
    va_end(ap);
    return a + b + c + d + e + f + g + h + i + x + y;
}

static int copied(...) {
    va_list ap, cp;
    va_start(ap);
    va_copy(cp, ap);
    int first  = va_arg(ap, int);
    int second = va_arg(cp, int);
    va_end(cp);
    va_end(ap);
    return first + second;
}

typedef struct {
    _Alignas(16) long x;
} Aligned16;

static int after_overaligned(int n, Aligned16 last, ...) {
    va_list ap;
    va_start(ap, last);
    int total = (int)last.x;
    for (int i = 0; i < n; i++)
        total += va_arg(ap, int);
    va_end(ap);
    return total;
}

static int after_overaligned_one_arg(int n, Aligned16 last, ...) {
    va_list ap;
    va_start(ap);
    int total = (int)last.x;
    for (int i = 0; i < n; i++)
        total += va_arg(ap, int);
    va_end(ap);
    return total;
}

static int extra_args(int n, ...) {
    va_list ap;
    va_start(ap, n, 1 + 2);
    int total = 0;
    for (int i = 0; i < n; i++)
        total += va_arg(ap, int);
    va_end(ap);
    return total;
}

static int extra_args_unparsed(int n, ...) {
    va_list ap;
    va_start(ap, n, not_declared anywhere);
    int total = 0;
    for (int i = 0; i < n; i++)
        total += va_arg(ap, int);
    va_end(ap);
    return total;
}

static int extra_args_builtin(int n, ...) {
    va_list ap;
    __builtin_va_start(ap, n, 0);
    int total = 0;
    for (int i = 0; i < n; i++)
        total += va_arg(ap, int);
    va_end(ap);
    return total;
}

#pragma cccc suite begin "va_start_c23"

[[cccc::test(return = 42)]]
int test_zero_named(void) {
    return sum_all(10, 12, 8, 12);
}

[[cccc::test(return = 42)]]
int test_zero_named_stack_tail(void) {
    // Ten variadic ints: the last two overflow the register area.
    return sum_n(10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10) - 13;
}

[[cccc::test(return = 42)]]
int test_one_arg_with_named(void) {
    return sum_n(3, 10, 20, 12);
}

[[cccc::test(return = 42)]]
int test_two_arg_unchanged(void) {
    return sum_n_two_arg(3, 10, 20, 12);
}

[[cccc::test(return = 42)]]
int test_mixed_types(void) {
    return (int)mixed(30, 3.0, 9L);
}

[[cccc::test(return = 42)]]
int test_nine_named(void) {
    // 1+2+..+9 = 45; x, y spill to the stack.
    return nine_named(1, 2, 3, 4, 5, 6, 7, 8, 9, 20, 22) - 45;
}

[[cccc::test(return = 42)]]
int test_va_copy(void) {
    return copied(21, 99);
}

[[cccc::test(return = 42)]]
int test_overaligned_last_two_arg(void) {
    Aligned16 a = {2};
    return after_overaligned(2, a, 10, 30);
}

[[cccc::test(return = 42)]]
int test_overaligned_last_one_arg(void) {
    Aligned16 a = {2};
    return after_overaligned_one_arg(2, a, 10, 30);
}

[[cccc::test(return = 42)]]
int test_extra_args_ignored(void) {
    return extra_args(3, 10, 20, 12);
}

[[cccc::test(return = 42)]]
int test_extra_args_not_parsed(void) {
    return extra_args_unparsed(3, 10, 20, 12);
}

[[cccc::test(return = 42)]]
int test_extra_args_builtin(void) {
    return extra_args_builtin(3, 10, 20, 12);
}

#pragma cccc suite end

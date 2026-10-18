// Comptime returns keep working: recursion, tail calls, longjmp across frames
// and a libc callback that re-enters the VM.
#include <setjmp.h>
#include <stdlib.h>

[[cccc::comptime]]
int fib(int n) {
    return n < 2 ? n : fib(n - 1) + fib(n - 2);
}

[[cccc::comptime]]
int count_down(int n, int acc) {
    if (n == 0)
        return acc;
    return count_down(n - 1, acc + 1);
}

[[cccc::comptime]] static jmp_buf env;

[[cccc::comptime]]
void deep(int n) {
    if (n == 0)
        longjmp(env, 7);
    deep(n - 1);
}

[[cccc::comptime]]
int cmp(const void *a, const void *b) {
    return *(const int *)a - *(const int *)b;
}

[[cccc::comptime]]
Node *total(void) {
    int sum    = fib(7) + count_down(5, 0); // 13 + 5
    int jumped = setjmp(env);
    if (jumped == 0)
        deep(4);
    int v[3] = {3, 1, 2};
    qsort(v, 3, sizeof v[0], cmp);
    return __builtin_ast_int_literal(sum + jumped + v[0] + v[2] +
                                     13); // 18 + 7 + 1 + 3 + 13
}

int main(void) {
    return total();
}

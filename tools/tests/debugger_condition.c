// Fixture for the conditional-breakpoint compile-and-run integration tests
// (ticket 113, see tools/test_debugger_condition.py). Each call site below
// is targeted by a distinct scenario: float args/returns, struct-by-value
// arguments, pointer-to-local arguments, indirect calls, nested-function
// static links, stack-passed (>8) arguments, plus the regression cases the
// old scalar-only evaluator already handled (integer locals, assignment,
// comma, member access) or rejected outright (float arithmetic, any call
// with a non-integer argument/return).

struct Point {
    int x;
    int y;
};

int add(int a, int b) {
    return a + b;
}
double half(double x) {
    return x / 2.0;
}
int add_ptr(int *p, int n) {
    return *p + n;
}
int sum_point(struct Point p) {
    return p.x + p.y;
}
int sum10(int a, int b, int c, int d, int e, int f, int g, int h, int i,
          int j) {
    return a + b + c + d + e + f + g + h + i + j;
}

int outer(int n) {
    int captured = n * 10;
    int inner(int x) {
        return x + captured;
    }
    int r = inner(1);
    return r;
}

int main(void) {
    int          x      = 10;
    double       fx     = 3.0;
    struct Point pt     = {3, 4};
    int (*fp)(int, int) = add;

    for (int i = 0; i < 5; i++) {
        x = x + 1;
    }

    fx        = fx + 1.5;
    int    r1 = add(x, 100);
    double r2 = half(fx);
    int    r3 = add_ptr(&x, 1);
    int    r4 = sum_point(pt);
    int    r5 = fp(3, 4);
    int    r6 = sum10(1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
    int    r7 = outer(2);

    return 42;
}

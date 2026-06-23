// Regression: dereferencing / accessing a member through a va_arg of pointer
// type must work.  Previously the va_arg macro typed a pointer result as
// double (the fp/non-fp arms were fused by a runtime "?:"), so "*va_arg(ap,T*)"
// reported "invalid pointer dereference".  va_arg now uses
// __builtin_choose_expr so each arm carries one consistent type.

#include <stdarg.h>

struct pt { int x, y; };

// Stores through int* arguments retrieved with va_arg (the libsqlite idiom
// "*va_arg(ap,int*) = val").
static void fill(int n, ...) {
    va_list ap;
    va_start(ap, n);
    for (int i = 0; i < n; i++)
        *va_arg(ap, int *) = i + 1;
    va_end(ap);
}

// Member access through a struct* retrieved with va_arg.
static int sum_first(int n, ...) {
    va_list ap;
    va_start(ap, n);
    struct pt *p = va_arg(ap, struct pt *);
    int s = p->x + p->y;
    va_end(ap);
    (void)n;
    return s;
}

int main(void) {
    int a = 0, b = 0, c = 0;
    fill(3, &a, &b, &c);
    if (a != 1 || b != 2 || c != 3) return 1;

    struct pt pt = { 30, 9 };
    if (sum_first(1, &pt) != 39) return 2;

    return a + b + c + 36;  // 1+2+3+36 = 42
}

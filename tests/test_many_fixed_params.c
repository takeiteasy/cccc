// Ticket #287: fixed parameters beyond the first 8 are stack-passed and
// copied into callee local slots.

#include <stdarg.h>

int sum9(int a, int b, int c, int d, int e, int f, int g, int h, int i) {
    int total = 0;
    total = total + a;
    total = total + b;
    total = total + c;
    total = total + d;
    total = total + e;
    total = total + f;
    total = total + g;
    total = total + h;
    total = total + i;
    return total;
}

int sum12(int a, int b, int c, int d, int e, int f, int g, int h, int i,
          int j, int k, int l) {
    int total = sum9(a, b, c, d, e, f, g, h, i);
    total = total + j;
    total = total + k;
    total = total + l;
    return total;
}

int sum16(int a, int b, int c, int d, int e, int f, int g, int h, int i,
          int j, int k, int l, int m, int n, int o, int p) {
    int total = sum12(a, b, c, d, e, f, g, h, i, j, k, l);
    total = total + m;
    total = total + n;
    total = total + o;
    total = total + p;
    return total;
}

int mixed_stack_float(int a, int b, int c, int d, int e, int f, int g, int h,
                      double i, float j, int k) {
    int total = sum9(a, b, c, d, e, f, g, h, (int)i);
    total = total + (int)j;
    total = total + k;
    return total;
}

int forward12(int a, int b, int c, int d, int e, int f, int g, int h, int i,
              int j, int k, int l) {
    return sum12(a, b, c, d, e, f, g, h, i, j, k, l);
}

int fixed9_then_varargs(int a, int b, int c, int d, int e, int f, int g,
                        int h, int i, ...) {
    va_list ap;
    va_start(ap, i);
    int j = va_arg(ap, int);
    int k = va_arg(ap, int);
    va_end(ap);
    int total = sum9(a, b, c, d, e, f, g, h, i);
    total = total + j;
    total = total + k;
    return total;
}

int main(void) {
    if (sum9(1, 2, 3, 4, 5, 6, 7, 8, 9) != 45)
        return 1;
    if (sum12(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12) != 78)
        return 2;
    if (sum16(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16) != 136)
        return 3;
    if (mixed_stack_float(1, 2, 3, 4, 5, 6, 7, 8, 9.0, 10.0f, 11) != 66)
        return 4;
    if (forward12(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12) != 78)
        return 5;
    if (fixed9_then_varargs(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11) != 66)
        return 6;
    return 42;
}

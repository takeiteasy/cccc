#include <stdarg.h>

int main(void) {
    int (^sum)(int, ...) = ^(int n, ...) {
      va_list ap;
      va_start(ap, n);
      int s = 0;
      for (int i = 0; i < n; i++)
          s += va_arg(ap, int);
      va_end(ap);
      return s;
    };
    if (sum(3, 10, 20, 12) != 42)
        return 1;

    double (^dsum)(int, ...) = ^(int n, ...) {
      va_list ap;
      va_start(ap, n);
      double s = 0.0;
      for (int i = 0; i < n; i++)
          s += va_arg(ap, double);
      va_end(ap);
      return s;
    };
    if (dsum(3, 1.5, 2.5f, 36.0) != 40.0)
        return 2;

    int base = 2;
    int (^cap)(int, ...) = ^(int n, ...) {
      va_list ap, bp;
      va_start(ap, n);
      va_copy(bp, ap);
      int s = base;
      for (int i = 0; i < n; i++)
          s += va_arg(ap, int);
      for (int i = 0; i < n; i++)
          s -= va_arg(bp, int);
      va_end(bp);
      va_end(ap);
      return s + 40;
    };
    if (cap(2, 7, 8) != 42)
        return 3;

    double (^mix)(double, int, ...) = ^(double d, int n, ...) {
      va_list ap;
      va_start(ap, n);
      double s = d;
      for (int i = 0; i < n; i++)
          s += va_arg(ap, int);
      va_end(ap);
      return s;
    };
    if (mix(2.0, 2, 20, 20) != 42.0)
        return 4;
    return 42;
}

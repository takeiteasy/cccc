#include <stdarg.h>

int main(void) {
    double (^half)(int) = ^double(int n) {
      return n / 2.0;
    };
    if (half(5) != 2.5)
        return 1;

    int  v        = 42;
    int *(^addr)(void) = ^int *(void) {
      return &v;
    };
    if (*addr() != 42)
        return 2;

    long (^sum)(int, ...) = ^long(int n, ...) {
      va_list ap;
      va_start(ap, n);
      long s = 0;
      for (int i = 0; i < n; i++)
          s += va_arg(ap, int);
      va_end(ap);
      return s;
    };
    if (sum(3, 10, 20, 12) != 42)
        return 3;
    return 42;
}

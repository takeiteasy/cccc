// va_start/va_arg/va_copy/va_end translate to real C under -c=native.
#include <stdarg.h>

static double sum_doubles(int count, ...) {
    va_list args;
    va_start(args, count);
    va_list copy;
    va_copy(copy, args);
    double copy_total = 0.0;
    for (int i = 0; i < count; i++)
        copy_total += va_arg(copy, double);
    va_end(copy);
    double total = 0.0;
    for (int i = 0; i < count; i++)
        total += va_arg(args, double);
    va_end(args);
    if (copy_total != total)
        return -1.0;
    return total;
}

int main(void) {
    double d = sum_doubles(4, 1.0, 2.0, 3.0, 4.0);
    if (d != 10.0)
        return 1;
    return 42;
}

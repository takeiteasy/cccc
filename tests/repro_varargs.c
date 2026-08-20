#include "stdarg.h"
#include "stdio.h"

int double_equal(double a, double b, double epsilon) {
    double diff = a - b;
    if (diff < 0.0)
        diff = -diff;
    return diff < epsilon;
}

double sum_many_doubles(int count, ...) {
    printf("sum_many_doubles: count=%d\n", count);
    va_list args;
    va_start(args, count);

    double sum = 0.0;
    for (int i = 0; i < count; i++) {
        double v = va_arg(args, double);
        printf("  arg[%d] = %f\n", i, v);
        sum += v;
    }

    va_end(args);
    printf("sum_many_doubles: sum=%f\n", sum);
    return sum;
}

int main() {
    // 11 args total = 1 count + 10 double varargs
    double dresult =
        sum_many_doubles(10, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0);
    printf("dresult = %f (expected 55.0)\n", dresult);
    if (!double_equal(dresult, 55.0, 0.0001))
        return 14;
    return 42;
}

// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: va_arg\(ap, double\)
// CCCC_EXPECT_STDOUT: va_arg\(ap, int\)
// CCCC_REJECT_STDOUT: va_arg\(ap, float\)
// CCCC_REJECT_STDOUT: va_arg\(ap, char\)
// -m output prints the promoted va_arg types.
#include <stdarg.h>

static float sum_floats(int n, ...) {
    va_list ap;
    va_start(ap, n);
    float total = 0.0f;
    for (int i = 0; i < n; i++)
        total += va_arg(ap, float);
    va_end(ap);
    return total;
}

static int sum_chars(int n, ...) {
    va_list ap;
    va_start(ap, n);
    int total = 0;
    for (int i = 0; i < n; i++)
        total += va_arg(ap, char);
    va_end(ap);
    return total;
}

int main(void) {
    if (sum_floats(2, 1.5f, 2.5f) != 4.0f)
        return 1;
    if (sum_chars(2, (char)20, (char)22) != 42)
        return 2;
    return 42;
}

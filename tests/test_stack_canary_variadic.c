// CCCC_FLAGS: --stack-canaries
// #445 — va_start must account for the reserved canary slot when counting
// register-spill slots (__CCCC_STACK_CANARIES__ correction in stdarg.h).
#include <stdarg.h>

int sum(int count, ...) {
    va_list ap;
    va_start(ap, count);
    int total = 0;
    for (int i = 0; i < count; i++)
        total += va_arg(ap, int);
    va_end(ap);
    return total;
}

int main(void) {
    if (sum(3, 10, 20, 12) != 42)
        return 1;
    // Enough varargs to spill past the register area into the caller stack area.
    if (sum(10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10) != 55)
        return 2;
    return 42;
}

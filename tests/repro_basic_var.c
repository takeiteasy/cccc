// Minimal: just count how many local slots are allocated
#include "stdarg.h"

int outer(int count, ...) {
    va_list args;                     // 3 slots (24 bytes struct)
    va_start(args, count);
    int total = 0;                    // 1 slot
    for (int i = 0; i < count; i++) { // i = 1 slot
        int val  = va_arg(args, int); // val = 1 slot
        total   += val;
    }
    va_end(args);
    return total;
    // Locals: args(3) + total(1) + i(1) + val(1) = 6 slots
    // Plus params(8) = 14 slots... but getting 24 or 23
}

int main() {
    int result = outer(3, 10, 20, 30);
    if (result == 60)
        return 42;
    return result;
}

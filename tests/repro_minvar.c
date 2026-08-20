// Check if va_list is correctly treated as struct for stack allocation
#include "stdarg.h"

int test(int count, ...) {
    // Only has: va_list args (3 slots) + 1 dummy + 8 params = 12 slots
    // But we're seeing 23... there must be other locals
    va_list args;
    int     dummy = 0;
    va_start(args, count);
    // va_start creates __bp (1 slot) + __param_slot (1 slot)?
    // Let's check what else is allocated...
    va_end(args);
    return dummy;
}

int main() {
    return test(1, 10);
}

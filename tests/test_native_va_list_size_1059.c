// sizeof(va_list) agrees between the VM and -c=native.
#include <stdarg.h>

int main(void) {
    if (sizeof(va_list) < 32)
        return 1;
    if (sizeof(va_list) != 64)
        return 2;
    return 42;
}

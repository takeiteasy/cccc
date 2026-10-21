// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: 64
// -m output folds sizeof(va_list).
#include <stdarg.h>

int main(void) {
    if (sizeof(va_list) < 32)
        return 1;
    if (sizeof(va_list) != 64)
        return 2;
    return 42;
}

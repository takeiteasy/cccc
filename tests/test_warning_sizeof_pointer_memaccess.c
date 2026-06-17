// CCCC_FLAGS: -Wsizeof-pointer-memaccess
// CCCC_EXPECT_STDERR: argument to 'memset' is the size of a pointer.*\[-Wsizeof-pointer-memaccess\]

#include <string.h>

int main(void) {
    char buf[64];
    char *p = buf;
    memset(p, 0, sizeof(p));
    return 42;
}

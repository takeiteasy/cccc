// CCCC_FLAGS: -Wno-sizeof-pointer-memaccess
// CCCC_REJECT_STDERR: sizeof-pointer-memaccess

#include <string.h>

int main(void) {
    char  buf[64];
    char *p = buf;
    memset(p, 0, sizeof(p));
    return 42;
}

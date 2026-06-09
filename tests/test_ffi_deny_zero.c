// CCCC_FLAGS: --ffi-deny=strlen
#include <string.h>

int main(void) {
    if (strlen("blocked") != 0)
        return 1;
    return 42;
}

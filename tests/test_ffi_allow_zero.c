// JCC_FLAGS: --ffi-allow=strlen
#include <stdio.h>
#include <string.h>

int main(void) {
    if (strlen("allowed") != 7)
        return 1;
    if (puts("blocked") != 0)
        return 2;
    return 42;
}

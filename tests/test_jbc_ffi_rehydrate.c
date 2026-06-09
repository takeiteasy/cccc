// Regression test: FFI deny policy must survive a .jbc save/load round-trip.
// CCCC_FLAGS: --ffi-deny=strlen
#include <string.h>

int main(void) {
    // strlen is in the deny list: blocked call returns 0
    if (strlen("hello") != 0)
        return 1;

    // strcmp is not denied: call works normally
    if (strcmp("hello", "hello") != 0)
        return 2;

    return 42;
}

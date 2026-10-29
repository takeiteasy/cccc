// CCCC_FLAGS: --use-system-headers --isystem /usr/include --isystem /usr/include/aarch64-linux-gnu --isystem /usr/include/x86_64-linux-gnu
// glibc's <limits.h> ends in `#include_next <limits.h>`, aimed at the
// compiler's own copy; CCCC's bundled header stands in for it.
#include <limits.h>

int main(void) {
    return CHAR_BIT == 8 && INT_MAX == 2147483647 && LONG_MAX > INT_MAX ? 42
                                                                        : 1;
}

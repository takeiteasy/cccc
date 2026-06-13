// Test: C23 %b/%B (binary integer) conversion specifier - printf family
// Returns: 42

#include "stdio.h"

int main() {
    // Basic %b / %B
    printf("%b\n", 42u);     // 101010
    printf("%B\n", 42u);     // 101010 (same digits, case only affects # prefix)
    printf("%b\n", 0u);      // 0
    printf("%b\n", 255u);    // 11111111

    // '#' flag adds 0b/0B prefix (and "0" for value 0, per C23)
    printf("%#b\n", 42u);    // 0b101010
    printf("%#B\n", 42u);    // 0B101010
    printf("%#b\n", 0u);     // 0

    // Field width + zero padding
    printf("%08b\n", 5u);    // 00000101
    printf("%#010b\n", 5u);  // 0b00000101

    // Left justification
    printf("[%-8b]\n", 5u);  // [101     ]

    // sprintf
    char buf[64];
    sprintf(buf, "%#b", 10u);
    printf("sprintf: %s\n", buf);

    // snprintf
    int n = snprintf(buf, sizeof(buf), "%b %#B", 6u, 7u);
    printf("snprintf: %s (n=%d)\n", buf, n);

    // fprintf to stdout
    fprintf(stdout, "%b\n", 1024u);  // 10000000000

    return 42;
}

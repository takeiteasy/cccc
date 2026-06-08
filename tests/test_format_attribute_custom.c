// JCC_FLAGS: -F
/*
 * Test __attribute__((format(printf, ...))) on custom functions
 */

#include "stdio.h"

int my_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2))) {
    return 42;
}

int my_fancy_printf(const char *label, const char *fmt, ...)
    __attribute__((format(printf, 2, 3))) {
    return 42;
}

int main(void) {
    // Valid calls — should pass without warning
    my_printf("Hello\n");
    my_printf("Number: %d\n", 42);
    my_printf("Two: %d %d\n", 10, 20);
    my_printf("String: %s\n", "test");
    my_printf("Mixed: %d %s\n", 42, "test");

    // Test with sprintf-style: label + format
    my_fancy_printf("INFO", "value = %d\n", 99);

    // Test %% — shouldn't consume args
    my_printf("100%% complete\n");

    return 42;
}

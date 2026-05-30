/*
 * Regression for ticket #119: foreign variadic calls must preserve all
 * source-order arguments, including stack-passed and floating-point slots.
 */

#include "stdio.h"
#include "string.h"

int main(void) {
    char buf[512];

    snprintf(buf, sizeof(buf),
             "ints:%d %d %d %d %d %d %d %d %d %d",
             1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
    if (strcmp(buf, "ints:1 2 3 4 5 6 7 8 9 10") != 0) {
        printf("bad ints: %s\n", buf);
        return 1;
    }

    snprintf(buf, sizeof(buf),
             "wide:%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
             1, 2, 3, 4, 5, 6, 7, 8,
             9, 10, 11, 12, 13, 14, 15, 16);
    if (strcmp(buf, "wide:1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16") != 0) {
        printf("bad wide: %s\n", buf);
        return 2;
    }

    snprintf(buf, sizeof(buf),
             "dbl:%.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f",
             1.0, 2.0, 3.0, 4.0, 5.0,
             6.0, 7.0, 8.0, 9.0, 10.0);
    if (strcmp(buf, "dbl:1.0 2.0 3.0 4.0 5.0 6.0 7.0 8.0 9.0 10.0") != 0) {
        printf("bad doubles: %s\n", buf);
        return 3;
    }

    snprintf(buf, sizeof(buf),
             "mix:%d %s %.2f %d %s %.2f %d %s %.2f %d",
             1, "two", 3.5, 4, "five", 6.25, 7, "eight", 9.75, 10);
    if (strcmp(buf, "mix:1 two 3.50 4 five 6.25 7 eight 9.75 10") != 0) {
        printf("bad mixed: %s\n", buf);
        return 4;
    }

    return 42;
}

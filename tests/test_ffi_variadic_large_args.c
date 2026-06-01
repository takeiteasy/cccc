/*
 * Regression for ticket #160: common FFI calls use a stack scratch buffer,
 * while calls with more than 32 source-order arguments still fall back safely.
 */

#include "stdio.h"
#include "string.h"

int main(void) {
    char buf[512];

    snprintf(buf, sizeof(buf),
             "%d %d %d %d %d %d %d %d %d %d "
             "%d %d %d %d %d %d %d %d %d %d "
             "%d %d %d %d %d %d %d %d %d %d "
             "%d %d %d %d %d",
             1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
             11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
             21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
             31, 32, 33, 34, 35);

    if (strcmp(buf,
               "1 2 3 4 5 6 7 8 9 10 "
               "11 12 13 14 15 16 17 18 19 20 "
               "21 22 23 24 25 26 27 28 29 30 "
               "31 32 33 34 35") != 0) {
        printf("bad large FFI args: %s\n", buf);
        return 1;
    }

    return 42;
}

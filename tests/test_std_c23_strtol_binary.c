// CCCC_FLAGS: --std=c23
// strtol/strtoll/strtoul/strtoull "0b"/"0B" prefix support (ticket #390)
#include <stdlib.h>
#include <string.h>

int main(void) {
    char *end;

    // base 0: 0b/0B prefix selects binary
    if (strtol("0b1010", &end, 0) != 10) return 1;
    if (*end != '\0') return 2;

    if (strtol("0B1010", &end, 0) != 10) return 3;

    // explicit base 2 also accepts the prefix
    if (strtol("0b1010", &end, 2) != 10) return 4;

    // negative
    if (strtol("-0b1010", &end, 2) != -10) return 5;

    // leading whitespace
    if (strtol("  0b101", &end, 0) != 5) return 6;

    // endptr points past consumed binary digits, to trailing junk
    long v = strtol("0b101xyz", &end, 0);
    if (v != 5) return 7;
    if (strcmp(end, "xyz") != 0) return 8;

    // non-prefixed inputs still work as before
    if (strtol("123", &end, 10) != 123) return 9;
    if (strtol("0x1F", &end, 0) != 31) return 10;
    if (strtol("017", &end, 0) != 15) return 11; // octal

    // strtoul / strtoull with 0b prefix
    if (strtoul("0b11", &end, 0) != 3) return 12;
    if (strtoull("0B11111111", &end, 0) != 255ull) return 13;

    // strtoll with 0b prefix
    if (strtoll("0b10000000000", &end, 0) != (1ll << 10)) return 14;

    // "0b" with no valid binary digit after it falls back (parses leading "0")
    v = strtol("0bz", &end, 0);
    if (v != 0) return 15;
    if (strcmp(end, "bz") != 0) return 16;

    return 42;
}

// The C23 "0b" prefix of the strtol family gives the same result in the VM and
// under -c=native, including the end pointer, through a function pointer, and
// for the unsigned variants.
#include <stdlib.h>

int main(void) {
    char *end;
    if (strtol("0b1010xyz", &end, 0) != 10 || *end != 'x')
        return 1;
    if (strtoll("-0B101", &end, 2) != -5 || *end != '\0')
        return 2;
    if (strtoul("  0b11", &end, 0) != 3 || *end != '\0')
        return 3;
    if (strtoull("0b111", NULL, 2) != 7)
        return 4;
    if (strtol("0b", &end, 0) != 0 || *end != 'b')
        return 5;
    if (strtol("0x1f", NULL, 0) != 31 || strtol("12", NULL, 10) != 12)
        return 6;
    long (*fn)(const char *, char **, int) = strtol;
    if (fn("0b110", &end, 0) != 6)
        return 7;
    return 42;
}

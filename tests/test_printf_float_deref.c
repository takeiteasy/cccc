// Direct regression test for the exact program reported in #712:
// a float array element read via subscript, passed as a variadic printf
// argument, produced no output because evaluating the float argument
// clobbered printf's format-string pointer already sitting in REG_A0
// (FREG_A0 and REG_A0 alias the same raw register number).
#include <stdio.h>
#include <string.h>

int main(void) {
    char  buf[64];
    float arr[4];
    arr[0] = 1.0f;
    snprintf(buf, sizeof(buf), "x=%f\n", arr[0]);
    if (strcmp(buf, "x=1.000000\n") != 0)
        return 1;
    return 42;
}

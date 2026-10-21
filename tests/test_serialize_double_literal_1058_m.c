// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: 55\.0
// -m output prints an integral double literal with its decimal point.
#include <stdio.h>
#include <string.h>

int main(void) {
    char buf[64];
    snprintf(buf, sizeof buf, "%g", 55.0);
    if (strcmp(buf, "55") != 0)
        return 1;
    snprintf(buf, sizeof buf, "%g %g %g", 0.0, 100.0, -58.0);
    if (strcmp(buf, "0 100 -58") != 0)
        return 2;
    return 42;
}

#include <stdio.h>
int report_1233(int x) {
    fprintf(stderr, "shim_used_1233: %d\n", x);
    return x;
}

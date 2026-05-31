#include <stdlib.h>
#include <time.h>

int main(void) {
    void *p = aligned_alloc(16, 64);
    if (!p) return 1;
    if (((unsigned long)p % 16) != 0) return 2;
    free(p);

    struct timespec ts;
    if (timespec_get(&ts, TIME_UTC) != TIME_UTC) return 3;
    if (ts.tv_sec <= 0) return 4;

    return 42;
}

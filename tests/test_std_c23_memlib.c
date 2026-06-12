// CCCC_FLAGS: --std=c23
// memset_explicit, memchr, memalignment, free_sized, free_aligned_sized,
// timegm (ticket #390)
#include <stdlib.h>
#include <string.h>
#include <time.h>

int main(void) {
    // memset_explicit
    char buf[8] = "abcdefg";
    memset_explicit(buf, 'X', 3);
    if (memcmp(buf, "XXXdefg", 7) != 0) return 1;
    if (memset_explicit(buf + 3, 'Y', 4) != buf + 3) return 2;
    if (memcmp(buf, "XXXYYYY", 8) != 0) return 3;

    // memchr
    const char *s = "hello world";
    const char *p = memchr(s, 'w', 11);
    if (p == NULL || *p != 'w') return 4;
    if (memchr(s, 'z', 11) != NULL) return 5;

    // memalignment
    if (memalignment(NULL) != 0) return 6;
    void *a16 = aligned_alloc(16, 64);
    if (memalignment(a16) < 16) return 7;
    void *a64 = aligned_alloc(64, 128);
    if (memalignment(a64) < 64) return 8;

    // free_sized / free_aligned_sized
    void *m = malloc(32);
    free_sized(m, 32);
    free_aligned_sized(a16, 16, 64);
    free_aligned_sized(a64, 64, 128);

    // timegm: 1970-01-01T00:00:00Z -> epoch 0
    struct tm t = {0};
    t.tm_year = 70;
    t.tm_mon = 0;
    t.tm_mday = 1;
    time_t epoch = timegm(&t);
    if (epoch != 0) return 9;

    // 2000-01-01T00:00:00Z -> 946684800
    struct tm t2 = {0};
    t2.tm_year = 100;
    t2.tm_mon = 0;
    t2.tm_mday = 1;
    time_t y2k = timegm(&t2);
    if (y2k != 946684800) return 10;

    return 42;
}

#include <string.h>

static void copy_bytes(char *to, const char *from, int count) {
    if (count <= 0) return;
    int n = (count + 7) / 8;
    switch (count % 8) {
        do {
            case 0: *to++ = *from++;
            case 7: *to++ = *from++;
            case 6: *to++ = *from++;
            case 5: *to++ = *from++;
            case 4: *to++ = *from++;
            case 3: *to++ = *from++;
            case 2: *to++ = *from++;
            case 1: *to++ = *from++;
        } while (--n > 0);
    }
}

int main(void) {
    char src[] = "Hello, World!12345";
    char dst[20] = {0};
    copy_bytes(dst, src, 13);
    if (memcmp(dst, "Hello, World!", 13) != 0) return 1;
    char dst2[20] = {0};
    copy_bytes(dst2, src, 8);
    if (memcmp(dst2, "Hello, W", 8) != 0) return 2;
    char dst3[20] = {0};
    copy_bytes(dst3, src, 1);
    if (dst3[0] != 'H') return 3;
    return 42;
}

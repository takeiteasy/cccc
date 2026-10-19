// Expected return: 42
// C23 memset_explicit, memalignment, free_sized and free_aligned_sized, called
// directly and through function pointers.
#include <stdlib.h>
#include <string.h>

int main(void) {
    char buf[8] = {0};
    if (memset_explicit(buf, 'X', 3) != buf || buf[2] != 'X' || buf[3])
        return 1;
    _Alignas(64) char a64[64];
    if (memalignment(a64) < 64 || memalignment(NULL) != 0)
        return 2;
    free_sized(malloc(32), 32);
    free_aligned_sized(aligned_alloc(16, 64), 16, 64);

    void (*fs)(void *, size_t) = free_sized;
    fs(malloc(8), 8);
    void (*fas)(void *, size_t, size_t) = free_aligned_sized;
    fas(aligned_alloc(16, 32), 16, 32);
    void *(*mse)(void *, int, size_t) = memset_explicit;
    mse(buf, 'Y', 1);
    size_t (*ma)(const void *) = memalignment;
    if (ma(a64) < 64)
        return 3;
    return buf[0] == 'Y' ? 42 : 4;
}

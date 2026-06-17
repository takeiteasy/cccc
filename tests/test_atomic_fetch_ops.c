// Expected return: 42
#include <stdatomic.h>

int main(void) {
    _Atomic int x = 10;

    int old = atomic_fetch_add(&x, 5);
    if (old != 10) return 1;
    if (atomic_load(&x) != 15) return 2;

    old = atomic_fetch_sub(&x, 3);
    if (old != 15) return 3;
    if (atomic_load(&x) != 12) return 4;

    old = atomic_fetch_or(&x, 0x3);
    if (old != 12) return 5;
    if (atomic_load(&x) != 15) return 6; // 12|3 = 15

    old = atomic_fetch_and(&x, 0xc);
    if (old != 15) return 7;
    if (atomic_load(&x) != 12) return 8; // 15&12 = 12

    old = atomic_fetch_xor(&x, 0xf);
    if (old != 12) return 9;
    if (atomic_load(&x) != 3) return 10; // 0b1100^0b1111=0b0011=3

    return 42;
}

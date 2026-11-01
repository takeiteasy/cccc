#include <stdbool.h>

// An unsigned char / _Bool / unsigned int load must read as the value its
// type denotes in the full 64-bit register, through a pointer, a stack local
// and an indexed access, including for the high bit set.
typedef unsigned long long ull;
typedef long long          ll;

unsigned                   gu[3] = {0xFFFFFFFFu, 0x80000000u, 1u};
unsigned char              gc[3] = {0xFF, 0x80, 1};
bool                       gb[2] = {true, false};

[[gnu::noinline]] ull load_u32(const unsigned *p) {
    return *p;
}
[[gnu::noinline]] ull load_u8(const unsigned char *p) {
    return *p;
}
[[gnu::noinline]] ull load_bool(const bool *p) {
    return *p;
}
[[gnu::noinline]] ull index_u32(const unsigned *p, int i) {
    return p[i];
}
[[gnu::noinline]] ull index_u8(const unsigned char *p, int i) {
    return p[i];
}
[[gnu::noinline]] ull index_bool(const bool *p, int i) {
    return p[i];
}

// A volatile local stays in its stack slot, so it is read with a local load.
[[gnu::noinline]] ull local_u32(unsigned v) {
    volatile unsigned x = v;
    return x;
}
[[gnu::noinline]] ull local_u8(unsigned char v) {
    volatile unsigned char x = v;
    return x;
}
[[gnu::noinline]] ull local_bool(bool v) {
    volatile bool x = v;
    return x;
}

// A sign-extending load of the same bits must stay sign-extended.
[[gnu::noinline]] ll load_i32(const int *p) {
    return *p;
}
[[gnu::noinline]] ll load_i8(const signed char *p) {
    return *p;
}

// The loaded value feeds arithmetic that reads all 64 bits.
[[gnu::noinline]] ull widen_shift(const unsigned *p) {
    return (ull)*p << 4;
}
[[gnu::noinline]] int compare_u8(const unsigned char *p) {
    return *p > 127;
}

int main(void) {
    int         neg32 = -1;
    signed char neg8  = -1;
    unsigned    two31 = 0x80000000u;

    if (load_u32(&gu[0]) != 0xFFFFFFFFull ||
        load_u32(&gu[1]) != 0x80000000ull || load_u32(&gu[2]) != 1ull)
        return 1;
    if (load_u8(&gc[0]) != 0xFFull || load_u8(&gc[1]) != 0x80ull ||
        load_u8(&gc[2]) != 1ull)
        return 2;
    if (load_bool(&gb[0]) != 1ull || load_bool(&gb[1]) != 0ull)
        return 3;
    if (index_u32(gu, 0) != 0xFFFFFFFFull || index_u32(gu, 1) != 0x80000000ull)
        return 4;
    if (index_u8(gc, 0) != 0xFFull || index_u8(gc, 1) != 0x80ull)
        return 5;
    if (index_bool(gb, 0) != 1ull || index_bool(gb, 1) != 0ull)
        return 6;
    if (local_u32(0xFFFFFFFFu) != 0xFFFFFFFFull ||
        local_u32(0x80000000u) != 0x80000000ull)
        return 7;
    if (local_u8(0xFF) != 0xFFull || local_u8(0x80) != 0x80ull)
        return 8;
    if (local_bool(true) != 1ull || local_bool(false) != 0ull)
        return 9;
    if (load_i32(&neg32) != -1 ||
        load_i32((int *)&two31) != (ll)(int)0x80000000u)
        return 10;
    if (load_i8(&neg8) != -1)
        return 11;
    if (widen_shift(&gu[0]) != 0xFFFFFFFF0ull)
        return 12;
    if (!compare_u8(&gc[0]) || compare_u8(&gc[2]))
        return 13;
    return 42;
}

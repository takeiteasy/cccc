// Casting an unsigned scalar to a wide _BitInt zero-extends; a signed one
// sign-extends.
#include <stdbool.h>

static _BitInt(256) widen_ull(unsigned long long v) {
    return (_BitInt(256))v;
}

static _BitInt(256) widen_ll(long long v) {
    return (_BitInt(256))v;
}

int main(void) {
    unsigned long long x = 0xffffffffffffffffull;
    _BitInt(256) k       = (_BitInt(256))x;
    if (k < 0 || (k >> 64) != 0 || k != (((_BitInt(256))1 << 64) - 1))
        return 1;

    unsigned _BitInt(256) u = (unsigned _BitInt(256))x;
    if ((u >> 64) != 0)
        return 2;

    unsigned int  ui = 0xffffffffu;
    unsigned char uc = 0xff;
    if ((_BitInt(192))ui != 0xffffffffll || (_BitInt(192))uc != 255)
        return 3;

    long long sn                   = -1;
    unsigned _BitInt(256) all_ones = (unsigned _BitInt(256))sn;
    if (all_ones != ~(unsigned _BitInt(256))0)
        return 4;
    if (widen_ll(-5) != -5 || widen_ll(-5) >= 0)
        return 5;

    if (widen_ull(x) <= 0 || widen_ull(x) + 1 != ((_BitInt(256))1 << 64))
        return 6;

    int  dummy = 0;
    bool b     = true;
    if ((_BitInt(256)) & dummy == 0 || (_BitInt(256))b != 1)
        return 7;

    static const _BitInt(256) folded = (_BitInt(256))0xffffffffffffffffull;
    if (folded != k)
        return 8;
    return 42;
}

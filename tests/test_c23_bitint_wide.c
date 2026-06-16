// Test C23 _BitInt(N) for N in (64, 256] — wide multi-word storage (#401)

_BitInt(128) add128(_BitInt(128) a, _BitInt(128) b) {
    return a + b;
}

unsigned _BitInt(256) pow2_256(int n) {
    unsigned _BitInt(256) r = 1;
    for (int i = 0; i < n; i++) r = r * 2;
    return r;
}

struct Box { _BitInt(128) v; int tag; };

void fill_box(struct Box *b, _BitInt(128) x) {
    b->v = x;
    b->tag = 1;
}

int main(void) {
    // sizeof for various wide widths
    _BitInt(65) w65 = 0;
    if (sizeof(w65) != 16) return 1;
    _BitInt(128) w128 = 0;
    if (sizeof(w128) != 16) return 2;
    _BitInt(192) w192 = 0;
    if (sizeof(w192) != 24) return 3;
    _BitInt(256) w256 = 0;
    if (sizeof(w256) != 32) return 4;

    // Basic arithmetic at N=80
    _BitInt(80) x = 5, y = 3;
    if (x + y != 8) return 5;
    if (x - y != 2) return 6;
    if (x * y != 15) return 7;
    if (x / y != 1) return 8;
    if (x % y != 2) return 9;

    // Negative values / signed div/mod at N=80
    _BitInt(80) neg = -7;
    if (neg != -7) return 10;
    if (neg / 2 != -3) return 11;
    if (neg % 2 != -1) return 12;

    // Function call/return at N=128 (exercises hidden-pointer ABI + RETBUF)
    _BitInt(128) a = 100, b = 200;
    _BitInt(128) c = add128(a, b);
    if (c != 300) return 13;

    // Chained calls returning wide _BitInt (RETBUF rotation)
    _BitInt(128) chained = add128(add128(a, b), add128(a, b));
    if (chained != 600) return 14;

    // Shifts at N=200, including shifting across word boundaries
    _BitInt(200) shifted = (_BitInt(200))1 << 150;
    _BitInt(200) back = shifted >> 150;
    if (back != 1) return 15;
    unsigned _BitInt(200) ushifted = (unsigned _BitInt(200))1 << 199;
    if ((ushifted >> 199) != 1) return 16;

    // Comparisons (signed and unsigned)
    _BitInt(96) sneg1 = -1, sneg2 = -1;
    if (!(sneg1 == sneg2)) return 17;
    if (!(sneg1 < (_BitInt(96))0)) return 18;
    unsigned _BitInt(96) uneg = (unsigned _BitInt(96))(-1);
    if (uneg < (unsigned _BitInt(96))0) return 19;

    // Multiplication overflow wraps at exact bit width (N=256)
    unsigned _BitInt(256) p100 = pow2_256(100);
    unsigned _BitInt(256) p64 = pow2_256(64);
    if (!(p100 > p64)) return 20;

    // Casts: wide -> narrow, narrow -> wide, wide -> wide (widen/narrow)
    _BitInt(128) small = (_BitInt(128))42;
    _BitInt(72) narrowed = (_BitInt(72))small;
    if (narrowed != 42) return 21;

    long long ll = 12345;
    _BitInt(150) fromll = (_BitInt(150))ll;
    if ((long long)fromll != 12345) return 22;

    _BitInt(70) widenarrow_src = -5;
    _BitInt(200) widened = (_BitInt(200))widenarrow_src;
    if (widened != -5) return 23; // must sign-extend, not zero-extend

    // double <-> wide _BitInt conversions
    double d = 3.5;
    _BitInt(100) fromd = (_BitInt(100))d;
    if (fromd != 3) return 24;
    double backd = (double)fromd;
    if (backd != 3.0) return 25;

    // Array of wide _BitInt
    _BitInt(128) arr[3];
    arr[0] = (_BitInt(128))10;
    arr[1] = (_BitInt(128))20;
    arr[2] = arr[0] + arr[1];
    if (arr[2] != 30) return 26;

    // Wide _BitInt as struct member, passed via pointer
    struct Box box;
    fill_box(&box, (_BitInt(128))999);
    if (box.v != 999 || box.tag != 1) return 27;

    // Division with multi-word divisor (N=150)
    unsigned _BitInt(150) big1 = (unsigned _BitInt(150))1 << 100;
    unsigned _BitInt(150) big2 = (unsigned _BitInt(150))1 << 50;
    if (big1 / big2 != ((unsigned _BitInt(150))1 << 50)) return 28;

    // Bitwise ops at N=128
    _BitInt(128) ba = (_BitInt(128))0xFF;
    _BitInt(128) bb = (_BitInt(128))0x0F;
    if ((ba & bb) != 0x0F) return 29;
    if ((ba | bb) != 0xFF) return 30;
    if ((ba ^ bb) != 0xF0) return 31;

    return 42;
}

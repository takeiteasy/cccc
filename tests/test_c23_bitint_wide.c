// Test C23 _BitInt(N) for N in (64, 65535] — wide multi-word storage (#401, #454)

_BitInt(128) add128(_BitInt(128) a, _BitInt(128) b) {
    return a + b;
}

unsigned _BitInt(256) pow2_256(int n) {
    unsigned _BitInt(256) r = 1;
    for (int i = 0; i < n; i++) r = r * 2;
    return r;
}

struct Box { _BitInt(128) v; int tag; };

unsigned _BitInt(4096) pow2_4096(int n) {
    unsigned _BitInt(4096) r = 1;
    for (int i = 0; i < n; i++) r = r * 2;
    return r;
}

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

    // sizeof for N > 256 (#454)
    _BitInt(300) w300 = 0;
    if (sizeof(w300) != 40) return 32;
    _BitInt(512) w512 = 0;
    if (sizeof(w512) != 64) return 33;
    _BitInt(1000) w1000 = 0;
    if (sizeof(w1000) != 128) return 34;
    _BitInt(4096) w4096 = 0;
    if (sizeof(w4096) != 512) return 35;
    _BitInt(65535) w65535 = 0;
    if (sizeof(w65535) != 8192) return 36;

    // Arithmetic at N=300 (5 words) — exercises udivmod's variable bounds
    _BitInt(300) x300 = 5, y300 = 3;
    if (x300 + y300 != 8) return 37;
    if (x300 - y300 != 2) return 38;
    if (x300 * y300 != 15) return 39;
    if (x300 / y300 != 1) return 40;
    if (x300 % y300 != 2) return 41;

    // Arithmetic at N=4096 (64 words)
    _BitInt(4096) x4096 = 5, y4096 = 3;
    if (x4096 + y4096 != 8) return 42;
    if (x4096 - y4096 != 2) return 43;
    if (x4096 * y4096 != 15) return 44;
    if (x4096 / y4096 != 1) return 45;
    if (x4096 % y4096 != 2) return 46;

    // Signed/unsigned comparison at N=512 (8 words)
    _BitInt(512) sneg512 = -1, szero512 = 0;
    if (!(sneg512 < szero512)) return 47;
    unsigned _BitInt(512) uneg512 = (unsigned _BitInt(512))(-1);
    if (uneg512 < (unsigned _BitInt(512))0) return 48;

    // Shift across many word boundaries at N=1000 (16 words)
    _BitInt(1000) shifted1000 = (_BitInt(1000))1 << 900;
    _BitInt(1000) back1000 = shifted1000 >> 900;
    if (back1000 != 1) return 49;
    _BitInt(1000) negshift = -1;
    _BitInt(1000) sshifted = negshift >> 900; // arithmetic shift keeps sign
    if (sshifted != -1) return 50;

    // int/double conversions at N=65535 (1024 words, worst-case stack usage)
    long long ll65535 = 123456;
    _BitInt(65535) fromll65535 = (_BitInt(65535))ll65535;
    if ((long long)fromll65535 != 123456) return 51;
    double d65535 = 7.0;
    _BitInt(65535) fromd65535 = (_BitInt(65535))d65535;
    if (fromd65535 != 7) return 52;
    if ((double)fromd65535 != 7.0) return 53;

    // Multiplication overflow wraps at exact bit width (N=4096, 64 words)
    unsigned _BitInt(4096) p4096_a = pow2_4096(4090);
    unsigned _BitInt(4096) p4096_b = pow2_4096(4000);
    if (!(p4096_a > p4096_b)) return 54;
    unsigned _BitInt(4096) wrap4096 = pow2_4096(4096); // 2^4096 mod 2^4096 == 0
    if (wrap4096 != 0) return 55;

    // --- WIDE_* opcode coverage (#456): +,-,*,/,%,<<,>>,>>> via opcodes ---

    // Unsigned div/mod (selects WIDE_DIV/WIDE_MOD's is_signed=0 path)
    unsigned _BitInt(80) ux = 17, uy = 5;
    if (ux / uy != 3) return 56;
    if (ux % uy != 2) return 57;

    // Signed div/mod with negative operands (is_signed=1 path)
    _BitInt(80) sx = -17, sy = 5;
    if (sx / sy != -3) return 58;
    if (sx % sy != -2) return 59;

    // Logical (unsigned) vs arithmetic (signed) shift on the same bit
    // pattern, to distinguish WIDE_SHR from WIDE_USHR.
    _BitInt(96) negval = -1;
    if ((negval >> 1) != -1) return 60; // arithmetic: sign-extends
    unsigned _BitInt(96) unegval = (unsigned _BitInt(96))(-1);
    if ((unegval >> 1) >= unegval) return 61; // logical: shifts in zero

    // Nested wide expression: (a + b) * c — multiple chained WIDE_* opcodes
    // with intermediate temporaries, exercising restrict-cache invalidation
    // and temp-register reuse across consecutive wide ops.
    _BitInt(150) na = 3, nb = 4, nc = 5;
    _BitInt(150) nested = (na + nb) * nc;
    if (nested != 35) return 62;

    // Wide binop result passed directly as a function argument (forces the
    // result through the same hidden-pointer ABI as add128's parameters,
    // right after a WIDE_ADD wrote it).
    _BitInt(128) argres = add128(a + (_BitInt(128))1, b);
    if (argres != 301) return 63;

    // Multiple simultaneously-live wide temporaries in one expression.
    _BitInt(128) t1 = (_BitInt(128))10, t2 = (_BitInt(128))20,
                 t3 = (_BitInt(128))30, t4 = (_BitInt(128))40;
    _BitInt(128) multi = (t1 + t2) + (t3 + t4);
    if (multi != 100) return 64;

    return 42;
}

// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: : USHRI3
// CCCC_REJECT_STDOUT: (USHRI3|USHR3|UDIV3|UMOD3) [^:]*: ZX4
// An unsigned shift right, divide or remainder of a value already in 32 bits
// stays in 32 bits, so converting it back to unsigned needs no extension.
[[gnu::noinline]] unsigned shr7(unsigned *p) {
    return *p >> 7;
}
[[gnu::noinline]] unsigned shr0(unsigned *p) {
    return *p >> 0;
}
[[gnu::noinline]] unsigned byte2(unsigned *p) {
    return (*p >> 16) & 255;
}
[[gnu::noinline]] unsigned shr_n(unsigned *p, unsigned n) {
    return *p >> n;
}
[[gnu::noinline]] unsigned div_u(unsigned *p, unsigned *q) {
    return *p / *q;
}
[[gnu::noinline]] unsigned mod_u(unsigned *p, unsigned *q) {
    return *p % *q;
}

int main(void) {
    unsigned top = 0xFFFFFFFFu, three = 3, ten = 10;
    if (shr7(&top) != 0x1FFFFFFu || shr0(&top) != top ||
        byte2(&top) != 255 || shr_n(&top, 31) != 1 ||
        div_u(&top, &three) != 0x55555555u || mod_u(&top, &ten) != 5)
        return 1;
    return 42;
}

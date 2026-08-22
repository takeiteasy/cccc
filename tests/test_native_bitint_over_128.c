// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -m
// CCCC_EXPECT_STDERR: exceeds 128 bits
//
// #1121: -c=native/-m emits __int128/unsigned __int128 for _BitInt(65..128)
// (serialize.c's TY_BITINT arm), but has no multi-word lowering beyond that
// -- a _BitInt(N>128) global/local must fail loudly rather than silently
// truncate into the wrong-width container (the #824 no-lossy-emulation
// policy). The VM's own address-based wide_bitint.c path still handles any
// width up to BITINT_MAXWIDTH; only -c=native/-m are restricted here.
unsigned _BitInt(256) pow2_256(int n) {
    unsigned _BitInt(256) r = 1;
    for (int i = 0; i < n; i++)
        r = r * 2;
    return r;
}

int main(void) {
    return (int)pow2_256(4);
}

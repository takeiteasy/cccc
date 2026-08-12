// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: __builtin_popcountll\(
// CCCC_REJECT_STDOUT: unsupported expr kind
//
// The bit-manipulation builtins must serialize back to the identical host
// builtin rather than vanishing into an `/* unsupported expr kind */`
// comment. The `ll` variants are the interesting half: popcount and parity
// encode a width of 0 in the node (unlike clz/ctz/ffs, which record 32/64,
// and bswap, which records a byte count), so the 64-bit spelling has to be
// recovered from the argument's own type. Emitting `__builtin_popcount` for
// a 64-bit argument would compile cleanly and silently truncate.

int main(void) {
    int a = __builtin_popcount(7);
    int b = __builtin_clz(1u);
    int c = __builtin_ctz(8u);
    int d = __builtin_ffs(4);
    unsigned e = __builtin_bswap32(0x01000000);
    long long w = 0xFFFFFFFFFFLL;
    int g = __builtin_popcountll(w);
    return a + b + c + d + (int)e + g - 39;
}

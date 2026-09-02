// #1121 previously made -c=native/-m hard-error on any _BitInt(N>128)
// ("exceeds 128 bits, which has no native/-m lowering") rather than silently
// truncate into the wrong-width container (the #824 no-lossy-emulation
// policy) -- this file used to pin that refusal as a compile-error case
// asserting on that stderr text. #1123 replaced the refusal with a real
// multi-word lowering (an emitted __cccc_biK container plus the
// src/stdlib/wide_bitint.c runtime, shared verbatim with the VM), so this is
// now a positive round-trip instead: the same construct the refusal used to
// name, actually running to completion, under both the VM and -c=native/-m.
// See tests/test_native_wide_bitint_1123.c for the dedicated lowering
// coverage (arithmetic, bitwise, shifts, comparisons, all cast directions,
// truthiness, struct members).
unsigned _BitInt(256) pow2_256(int n) {
    unsigned _BitInt(256) r = 1;
    for (int i = 0; i < n; i++)
        r = r * 2;
    return r;
}

int main(void) {
    if (pow2_256(4) != 16)
        return 1;
    return 42;
}

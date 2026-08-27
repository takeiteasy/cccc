// Test: an UNNAMED bit-field carrying an explicit
// __attribute__((aligned(N))) raises the enclosing struct's own alignment
// (and, through it, its size) only under real AAPCS64, the same
// CCCC_ALIGN_ANON_BITFIELDS split (src/parse_types.c) governing every
// other unnamed-bit-field shape -- not a gcc-vs-clang split as #1165
// originally concluded. A named bit-field's explicit alignment always
// raises it, on every target (see test_suite_attributes_layout_1129.c's
// test_bitfield_suffix_aligned_1165).
// Expected return: 42
//
// #1165 follow-up: gcc-16/clang both place the following member `c` at
// offset 20 on every target. Under real AAPCS64 (Linux/aarch64, either
// compiler family) and macOS/arm64 gcc-16 (a WONT_FIX outlier -- gcc never
// implemented Apple's AAPCS64 deviation), the struct's own alignment/size
// raise to 16/32; under x86_64 (either compiler family) and Darwin/arm64
// clang, they stay at 4/24 (verified directly, all combinations). Because
// `-c=native`'s default host `cc` is clang on Darwin, this is pinned as a
// VM-only test (not the native-round-tripped attributes_layout suite)
// rather than asserting sizeof/_Alignof there, where it would fail the
// native round-trip on a Darwin-gcc host and look like a cccc bug.

#include "stddef.h"

struct UnnamedAlignedBitfield1165 {
    char a;
    int : 5 __attribute__((aligned(16)));
    int c;
};

int main(void) {
    if (offsetof(struct UnnamedAlignedBitfield1165, c) != 20)
        return 1;
#if defined(__aarch64__) && !defined(__APPLE__)
    if (sizeof(struct UnnamedAlignedBitfield1165) != 32)
        return 2;
    if (_Alignof(struct UnnamedAlignedBitfield1165) != 16)
        return 3;
#else
    if (sizeof(struct UnnamedAlignedBitfield1165) != 24)
        return 2;
    if (_Alignof(struct UnnamedAlignedBitfield1165) != 4)
        return 3;
#endif
    return 42;
}

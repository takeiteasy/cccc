// Test: an UNNAMED bit-field carrying an explicit
// __attribute__((aligned(N))) raises the enclosing struct's own alignment
// (and, through it, its size), the same as a named bit-field's explicit
// alignment does (see test_suite_attributes_layout_1129.c's
// test_bitfield_suffix_aligned_1165).
// Expected return: 42
//
// #1165: gcc-16 and clang disagree here specifically -- both place the
// following member `c` at offset 20, but gcc raises the struct's own
// alignment/size to 16/32 while clang leaves them at 4/24 (verified
// directly against both compilers). cccc follows gcc, matching this
// project's reference compiler used throughout parse_types.c's bit-field
// layout comments. Because `-c=native` uses clang on macOS, this
// divergent case is deliberately pinned only here (a VM-only test, not the
// native-round-tripped attributes_layout suite) rather than asserting
// sizeof/_Alignof there, where it would fail the native round-trip on this
// host and look like a cccc bug.

#include "stddef.h"

struct UnnamedAlignedBitfield1165 {
    char a;
    int : 5 __attribute__((aligned(16)));
    int c;
};

int main(void) {
    if (offsetof(struct UnnamedAlignedBitfield1165, c) != 20)
        return 1;
    if (sizeof(struct UnnamedAlignedBitfield1165) != 32)
        return 2;
    if (_Alignof(struct UnnamedAlignedBitfield1165) != 16)
        return 3;
    return 42;
}

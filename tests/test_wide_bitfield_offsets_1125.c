// CCCC_FLAGS: --testing
//
// #1125: a bitfield whose declared type is a wide _BitInt (N > 64) used to
// crash the VM outright -- emit_load_ex's wide-_BitInt arm hands back the
// storage unit's *address* unchanged (wide values are address-based
// everywhere else in this compiler), and the ordinary scalar bitfield
// shift/mask code then ran on that address as if it were a value,
// corrupting it before any consumer dereferenced it. On the write side, a
// bitfield's assignment type is its container type, so it took the
// generic struct/union/wide-_BitInt MCPY fast path in ND_ASSIGN, which
// ignores bit_offset entirely and can write past the struct (bitfields are
// laid out compactly: a `_BitInt(256) f : 193;` struct is 25 bytes, though
// its container spans 32). Fixed with two byte-granular runtime helpers,
// __cccc_bitfield_extract/__cccc_bitfield_insert (src/stdlib/wide_bitint.c).
//
// Kept standalone and local-variable-only (not folded into
// tests/suites/test_suite_typesystem.c's own bit_offset>0 coverage, which
// additionally checks a *global* of the same struct) so this file's own
// #1125 codegen coverage stays isolated from that suite's broader surface.
struct WideBitfieldOffsets1125 {
    _BitInt(128) a : 5;   // bit_offset 0
    _BitInt(128) b : 100; // bit_offset 5 -- straddles into word 1
};
struct WideBitfieldShared1125 {
    unsigned _BitInt(128) a : 3;
    unsigned _BitInt(128) b : 3; // bit_offset 3, packed into the same byte
};

[[cccc::test(return = 42)]]
int test_wide_bitfield_offsets(void) {
    struct WideBitfieldOffsets1125 l;
    l.a             = -3;
    l.b             = ((_BitInt(128))1 << 90) + 7;

    _BitInt(128) va = l.a;
    if (va != -3)
        return 1;
    _BitInt(128) vb = l.b;
    if (vb != ((_BitInt(128))1 << 90) + 7)
        return 2;

    // Two narrow fields packed into the same wide container.
    struct WideBitfieldShared1125 s;
    s.a                      = 5;
    s.b                      = 6;
    unsigned _BitInt(128) sa = s.a, sb = s.b;
    if (sa != 5 || sb != 6)
        return 3;

    return 42;
}

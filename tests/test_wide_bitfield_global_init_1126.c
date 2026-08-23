// CCCC_FLAGS: --testing
//
// #1126: serialize_init_bytes()'s TY_STRUCT bitfield arm read a global's
// bitfield value back out of the folded init_data byte image with a memcpy
// clamped to 8 bytes regardless of the member's own container size (16 for
// a wide-_BitInt-typed bitfield, e.g. `_BitInt(128) f : 100;`), then printed
// the result as a plain %llu literal -- so any bit at or above bit 64 of
// the field's own value was silently dropped under -c=native/-m. This is
// distinct from #1125 (runtime bitfield read/write codegen, already correct
// natively -- the host compiler lays out the real bitfield) and #1122
// (write_gvar_data's byte-image fold, also correct -- this is the
// re-extraction on the *print* side only). Fixed with a byte-granular
// extract over the field's exact [bit_offset, bit_offset+bit_width) span,
// mirroring __cccc_bitfield_extract (src/stdlib/wide_bitint.c).
//
// Covers what tests/suites/test_suite_typesystem.c's own
// test_wide_global_init (case 12, from #1122) and
// test_wide_bitfield_global_offset already assert (both of which this bug
// silently held off the native corpus), plus a signed negative value and a
// value that fits under 64 bits, to pin the three print arms (%lluu,
// %lld, and the 128-bit hex literal) directly.
struct WideBitfieldGlobalInit1126 {
    _BitInt(128) a          : 100; // bit_offset 0, value spans past bit 64
    _BitInt(128) b          : 20;  // bit_offset 100 -- fits under 64 bits
    unsigned _BitInt(128) c : 100; // bit_offset 0 of its own struct, unsigned
};
static struct WideBitfieldGlobalInit1126 g_wbgi = {
    ((_BitInt(128))1 << 90) + 7, -12345, ((unsigned _BitInt(128))1 << 90) + 7};

// bit_width == 64 exactly: the sign-extended minimum value hits the
// "no negative literal wide enough" hazard the fix's hex-literal arm
// exists for (%lld of a sign-extended INT64_MIN is not a valid `long long`
// constant).
struct WideBitfield64_1126 {
    _BitInt(128) f : 64;
};
static struct WideBitfield64_1126 g_wb64 = {-((_BitInt(128))1 << 63)};

[[cccc::test(return = 42)]]
int test_wide_bitfield_global_init(void) {
    struct WideBitfieldGlobalInit1126 l;
    l.a = ((_BitInt(128))1 << 90) + 7;
    l.b = -12345;
    l.c = ((unsigned _BitInt(128))1 << 90) + 7;

    if (g_wbgi.a != l.a)
        return 1;
    if (g_wbgi.b != l.b)
        return 2;
    if (g_wbgi.c != l.c)
        return 3;

    struct WideBitfield64_1126 l64;
    l64.f = -((_BitInt(128))1 << 63);
    if (g_wb64.f != l64.f)
        return 4;

    return 42;
}

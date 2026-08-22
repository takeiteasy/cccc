// CCCC_FLAGS: --testing
//
// #1124: the native/-m serializer applied no _BitInt(N) width masking --
// serialize_type's TY_BITINT case (src/serialize.c) picks a container
// purely from ty->size (a plain char/short/int/long/__int128), and nothing
// re-masked a computed value back down to N bits the way the VM's own
// emit_bitint_trunc (src/codegen_emit.c) does. Every width below is chosen
// so N != the container's own bit width (5, 7, 9, 17, 100), so a masking
// bug here would silently diverge between the VM and -c=native/-m instead
// of merely being redundant with the container's own truncation.
//
// Kept standalone (not folded into tests/suites/test_suite_typesystem.c,
// where the equivalent bitfield coverage for #1125 lives) specifically so
// it stays on the native corpus independent of that suite's own #1126
// skip-list entry -- #1124 is entirely about native/-m behavior, so losing
// automated native coverage for it would be the wrong trade.

[[cccc::test(return = 42)]]
int test_bitint_width_semantics(void) {
    unsigned _BitInt(5) u5 = 31;
    u5                     = u5 + 1; // 32 truncates to 5 bits -> 0
    if (u5 != 0)
        return 1;

    _BitInt(7) s7 = 63;
    s7            = s7 + 1; // 64 truncates+sign-extends to 7 bits -> -64
    if (s7 != -64)
        return 2;

    unsigned _BitInt(9) u9 = 511;
    u9                     = u9 + 1;
    if (u9 != 0)
        return 3;

    _BitInt(17) s17 = -1;
    s17             = s17 * 2; // stays -2 within 17 bits
    if (s17 != -2)
        return 4;

    // A container in (64, 128] (host __int128) -- same shift-pair, wider
    // computation width.
    _BitInt(100) s100 = ((_BitInt(100))1 << 99) - 1; // INT100_MAX
    s100              = s100 + 1;                    // wraps to INT100_MIN
    if (s100 != -((_BitInt(100))1 << 99))
        return 5;

    unsigned _BitInt(100) u100 = ((unsigned _BitInt(100))1 << 99);
    u100                       = u100 * 3; // 3*2^99 mod 2^100 == 2^99
    if (u100 != ((unsigned _BitInt(100))1 << 99))
        return 6;

    // ~x / -x on a narrow field must also stay masked to N bits.
    unsigned _BitInt(5) u5b = ~(unsigned _BitInt(5))0; // all-ones, 5 bits -> 31
    if (u5b != 31)
        return 7;
    _BitInt(7) s7b = -(_BitInt(7))(-64); // -(-64) overflows 7-bit signed range
    if (s7b != -64)
        return 8;

    return 42;
}

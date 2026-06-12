// CCCC_FLAGS: --std=c23
// <stdbit.h> - C23 bit manipulation, core 32/64-bit operations (ticket #390)
#include <stdbit.h>

int main(void) {
    // stdc_leading_zeros
    if (stdc_leading_zeros_ui(0u) != 32) return 1;
    if (stdc_leading_zeros_ui(1u) != 31) return 2;
    if (stdc_leading_zeros_ull(0ull) != 64) return 3;
    if (stdc_leading_zeros_ull(1ull) != 63) return 4;

    // stdc_trailing_zeros
    if (stdc_trailing_zeros_ui(0u) != 32) return 5;
    if (stdc_trailing_zeros_ui(8u) != 3) return 6;
    if (stdc_trailing_zeros_ull(0ull) != 64) return 7;
    if (stdc_trailing_zeros_ull(8ull) != 3) return 8;

    // stdc_count_ones
    if (stdc_count_ones_ui(0u) != 0) return 9;
    if (stdc_count_ones_ui(0xFFu) != 8) return 10;
    if (stdc_count_ones_ull(0xFFFFFFFFFFFFFFFFull) != 64) return 11;

    // stdc_bit_width
    if (stdc_bit_width_ui(0u) != 0) return 12;
    if (stdc_bit_width_ui(1u) != 1) return 13;
    if (stdc_bit_width_ui(5u) != 3) return 14;
    if (stdc_bit_width_ull(0ull) != 0) return 15;
    if (stdc_bit_width_ull(0x100000000ull) != 33) return 16;

    // stdc_has_single_bit
    if (stdc_has_single_bit_ui(0u)) return 17;
    if (!stdc_has_single_bit_ui(1u)) return 18;
    if (!stdc_has_single_bit_ui(8u)) return 19;
    if (stdc_has_single_bit_ui(6u)) return 20;
    if (!stdc_has_single_bit_ull(0x100000000ull)) return 21;

    // stdc_bit_floor
    if (stdc_bit_floor_ui(0u) != 0) return 22;
    if (stdc_bit_floor_ui(1u) != 1) return 23;
    if (stdc_bit_floor_ui(5u) != 4) return 24;
    if (stdc_bit_floor_ui(8u) != 8) return 25;
    if (stdc_bit_floor_ull(0x180000000ull) != 0x100000000ull) return 26;

    // stdc_bit_ceil
    if (stdc_bit_ceil_ui(0u) != 1) return 27;
    if (stdc_bit_ceil_ui(1u) != 1) return 28;
    if (stdc_bit_ceil_ui(5u) != 8) return 29;
    if (stdc_bit_ceil_ui(8u) != 8) return 30;
    // largest power of two representable in 32 bits
    if (stdc_bit_ceil_ui(0x80000000u) != 0x80000000u) return 31;
    // not representable -> implementation-defined result of 0
    if (stdc_bit_ceil_ui(0x80000001u) != 0) return 32;
    if (stdc_bit_ceil_ull(0x180000000ull) != 0x200000000ull) return 33;

    // _ul variant (64-bit on this platform)
    if (stdc_leading_zeros_ul(1ul) != 63) return 34;
    if (stdc_bit_width_ul(0x100000000ul) != 33) return 35;

    return 42;
}

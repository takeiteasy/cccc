// CCCC_FLAGS: --std=c23
// <stdbit.h> - C23 bit manipulation, full coverage (tickets #390, #393)
#include <stdbit.h>

int main(void) {
    /* ---- stdc_leading_zeros ---- */
    if (stdc_leading_zeros_uc((unsigned char)0) != 8) return 1;
    if (stdc_leading_zeros_uc((unsigned char)1) != 7) return 2;
    if (stdc_leading_zeros_uc((unsigned char)0x80) != 0) return 3;
    if (stdc_leading_zeros_us((unsigned short)0) != 16) return 4;
    if (stdc_leading_zeros_us((unsigned short)1) != 15) return 5;
    if (stdc_leading_zeros_ui(0u) != 32) return 6;
    if (stdc_leading_zeros_ui(1u) != 31) return 7;
    if (stdc_leading_zeros_ull(0ull) != 64) return 8;
    if (stdc_leading_zeros_ull(1ull) != 63) return 9;
    if (stdc_leading_zeros_ul(1ul) != 63) return 10;

    /* ---- stdc_trailing_zeros ---- */
    if (stdc_trailing_zeros_uc((unsigned char)0) != 8) return 11;
    if (stdc_trailing_zeros_uc((unsigned char)8) != 3) return 12;
    if (stdc_trailing_zeros_us((unsigned short)0) != 16) return 13;
    if (stdc_trailing_zeros_us((unsigned short)8) != 3) return 14;
    if (stdc_trailing_zeros_ui(0u) != 32) return 15;
    if (stdc_trailing_zeros_ui(8u) != 3) return 16;
    if (stdc_trailing_zeros_ull(0ull) != 64) return 17;
    if (stdc_trailing_zeros_ull(8ull) != 3) return 18;

    /* ---- stdc_leading_ones ---- */
    if (stdc_leading_ones_uc((unsigned char)0xFF) != 8) return 19;
    if (stdc_leading_ones_uc((unsigned char)0xF0) != 4) return 20;
    if (stdc_leading_ones_uc((unsigned char)0) != 0) return 21;
    if (stdc_leading_ones_us((unsigned short)0xFFFF) != 16) return 22;
    if (stdc_leading_ones_us((unsigned short)0xFF00) != 8) return 23;
    if (stdc_leading_ones_ui(0xFFFFFFFFu) != 32) return 24;
    if (stdc_leading_ones_ui(0xF0000000u) != 4) return 25;
    if (stdc_leading_ones_ull(0xFFFFFFFFFFFFFFFFull) != 64) return 26;

    /* ---- stdc_trailing_ones ---- */
    if (stdc_trailing_ones_uc((unsigned char)0xFF) != 8) return 27;
    if (stdc_trailing_ones_uc((unsigned char)0x0F) != 4) return 28;
    if (stdc_trailing_ones_uc((unsigned char)0) != 0) return 29;
    if (stdc_trailing_ones_ui(0xFFFFFFFFu) != 32) return 30;
    if (stdc_trailing_ones_ui(7u) != 3) return 31;
    if (stdc_trailing_ones_ull(0xFFFFFFFFFFFFFFFFull) != 64) return 32;

    /* ---- stdc_count_ones ---- */
    if (stdc_count_ones_uc((unsigned char)0) != 0) return 33;
    if (stdc_count_ones_uc((unsigned char)0xFF) != 8) return 34;
    if (stdc_count_ones_us((unsigned short)0xFFFF) != 16) return 35;
    if (stdc_count_ones_ui(0u) != 0) return 36;
    if (stdc_count_ones_ui(0xFFu) != 8) return 37;
    if (stdc_count_ones_ull(0xFFFFFFFFFFFFFFFFull) != 64) return 38;

    /* ---- stdc_count_zeros ---- */
    if (stdc_count_zeros_uc((unsigned char)0xFF) != 0) return 39;
    if (stdc_count_zeros_uc((unsigned char)0) != 8) return 40;
    if (stdc_count_zeros_uc((unsigned char)0xF0) != 4) return 41;
    if (stdc_count_zeros_us((unsigned short)0) != 16) return 42;
    if (stdc_count_zeros_ui(0u) != 32) return 43;
    if (stdc_count_zeros_ui(0xFFFFFFFFu) != 0) return 44;
    if (stdc_count_zeros_ull(0xFFFFFFFFFFFFFFFFull) != 0) return 45;
    if (stdc_count_zeros_ull(0ull) != 64) return 46;

    /* ---- stdc_bit_width ---- */
    if (stdc_bit_width_uc((unsigned char)0) != 0) return 47;
    if (stdc_bit_width_uc((unsigned char)1) != 1) return 48;
    if (stdc_bit_width_uc((unsigned char)255) != 8) return 49;
    if (stdc_bit_width_us((unsigned short)0) != 0) return 50;
    if (stdc_bit_width_us((unsigned short)256) != 9) return 51;
    if (stdc_bit_width_ui(0u) != 0) return 52;
    if (stdc_bit_width_ui(1u) != 1) return 53;
    if (stdc_bit_width_ui(5u) != 3) return 54;
    if (stdc_bit_width_ull(0ull) != 0) return 55;
    if (stdc_bit_width_ull(0x100000000ull) != 33) return 56;
    if (stdc_bit_width_ul(0x100000000ul) != 33) return 57;

    /* ---- stdc_has_single_bit ---- */
    if (stdc_has_single_bit_uc((unsigned char)0)) return 58;
    if (!stdc_has_single_bit_uc((unsigned char)1)) return 59;
    if (!stdc_has_single_bit_uc((unsigned char)128)) return 60;
    if (stdc_has_single_bit_uc((unsigned char)3)) return 61;
    if (stdc_has_single_bit_ui(0u)) return 62;
    if (!stdc_has_single_bit_ui(1u)) return 63;
    if (!stdc_has_single_bit_ui(8u)) return 64;
    if (stdc_has_single_bit_ui(6u)) return 65;
    if (!stdc_has_single_bit_ull(0x100000000ull)) return 66;

    /* ---- stdc_bit_floor ---- */
    if (stdc_bit_floor_uc((unsigned char)0) != 0) return 67;
    if (stdc_bit_floor_uc((unsigned char)5) != 4) return 68;
    if (stdc_bit_floor_uc((unsigned char)128) != 128) return 69;
    if (stdc_bit_floor_ui(0u) != 0) return 70;
    if (stdc_bit_floor_ui(1u) != 1) return 71;
    if (stdc_bit_floor_ui(5u) != 4) return 72;
    if (stdc_bit_floor_ui(8u) != 8) return 73;
    if (stdc_bit_floor_ull(0x180000000ull) != 0x100000000ull) return 74;

    /* ---- stdc_bit_ceil ---- */
    if (stdc_bit_ceil_uc((unsigned char)0) != 1) return 75;
    if (stdc_bit_ceil_uc((unsigned char)5) != 8) return 76;
    if (stdc_bit_ceil_uc((unsigned char)128) != 128) return 77;
    if (stdc_bit_ceil_uc((unsigned char)129) != 0) return 78; /* unrepresentable */
    if (stdc_bit_ceil_ui(0u) != 1) return 79;
    if (stdc_bit_ceil_ui(1u) != 1) return 80;
    if (stdc_bit_ceil_ui(5u) != 8) return 81;
    if (stdc_bit_ceil_ui(8u) != 8) return 82;
    if (stdc_bit_ceil_ui(0x80000000u) != 0x80000000u) return 83;
    if (stdc_bit_ceil_ui(0x80000001u) != 0) return 84; /* unrepresentable */
    if (stdc_bit_ceil_ull(0x180000000ull) != 0x200000000ull) return 85;

    /* ---- stdc_first_leading_one ---- */
    if (stdc_first_leading_one_uc((unsigned char)0) != 0) return 86;
    if (stdc_first_leading_one_uc((unsigned char)0x80) != 1) return 87;
    if (stdc_first_leading_one_uc((unsigned char)0x40) != 2) return 88;
    if (stdc_first_leading_one_uc((unsigned char)1) != 8) return 89;
    if (stdc_first_leading_one_ui(0u) != 0) return 90;
    if (stdc_first_leading_one_ui(1u) != 32) return 91;
    if (stdc_first_leading_one_ui(0x80000000u) != 1) return 92;
    if (stdc_first_leading_one_ull(0ull) != 0) return 93;
    if (stdc_first_leading_one_ull(1ull) != 64) return 94;

    /* ---- stdc_first_leading_zero ---- */
    if (stdc_first_leading_zero_uc((unsigned char)0xFF) != 0) return 95;
    if (stdc_first_leading_zero_uc((unsigned char)0x7F) != 1) return 96;
    if (stdc_first_leading_zero_uc((unsigned char)0) != 1) return 97;
    if (stdc_first_leading_zero_ui(0xFFFFFFFFu) != 0) return 98;
    if (stdc_first_leading_zero_ui(0u) != 1) return 99;
    if (stdc_first_leading_zero_ull(0xFFFFFFFFFFFFFFFFull) != 0) return 100;

    /* ---- stdc_first_trailing_one ---- */
    if (stdc_first_trailing_one_uc((unsigned char)0) != 0) return 101;
    if (stdc_first_trailing_one_uc((unsigned char)1) != 1) return 102;
    if (stdc_first_trailing_one_uc((unsigned char)8) != 4) return 103;
    if (stdc_first_trailing_one_ui(0u) != 0) return 104;
    if (stdc_first_trailing_one_ui(8u) != 4) return 105;
    if (stdc_first_trailing_one_ull(0ull) != 0) return 106;
    if (stdc_first_trailing_one_ull(8ull) != 4) return 107;

    /* ---- stdc_first_trailing_zero ---- */
    if (stdc_first_trailing_zero_uc((unsigned char)0xFF) != 0) return 108;
    if (stdc_first_trailing_zero_uc((unsigned char)0xFE) != 1) return 109;
    if (stdc_first_trailing_zero_uc((unsigned char)0) != 1) return 110;
    if (stdc_first_trailing_zero_ui(0xFFFFFFFFu) != 0) return 111;
    if (stdc_first_trailing_zero_ui(0u) != 1) return 112;
    if (stdc_first_trailing_zero_ull(0xFFFFFFFFFFFFFFFFull) != 0) return 113;

    /* ---- _Generic dispatch ---- */
    unsigned char  uc = 0xF0;
    unsigned short us = 0xF000;
    unsigned int   ui = 0xF0000000u;
    unsigned long  ul = 0xF000000000000000ul;
    unsigned long long ull = 0xF000000000000000ull;

    if (stdc_leading_zeros(uc) != 0) return 114;
    if (stdc_leading_zeros(us) != 0) return 115;
    if (stdc_leading_zeros(ui) != 0) return 116;
    if (stdc_leading_zeros(ul) != 0) return 117;
    if (stdc_leading_zeros(ull) != 0) return 118;

    if (stdc_count_ones(uc) != 4) return 119;
    if (stdc_count_zeros(uc) != 4) return 120;
    if (stdc_has_single_bit(ui) != 0) return 121;

    if (stdc_bit_width((unsigned char)5) != 3) return 122;
    if (stdc_bit_floor((unsigned int)5u) != 4u) return 123;
    if (stdc_bit_ceil((unsigned int)5u) != 8u) return 124;

    if (stdc_first_leading_one(uc) != 1) return 125;
    if (stdc_first_leading_zero((unsigned char)0x7F) != 1) return 126;
    if (stdc_first_trailing_one((unsigned char)8) != 4) return 127;
    if (stdc_first_trailing_zero((unsigned char)0xFE) != 1) return 128;

    /* ---- Endian macros ---- */
#if __STDC_ENDIAN_NATIVE__ != __STDC_ENDIAN_LITTLE__ && \
    __STDC_ENDIAN_NATIVE__ != __STDC_ENDIAN_BIG__
    return 129;  /* NATIVE must be one of LITTLE or BIG */
#endif
#if __STDC_ENDIAN_LITTLE__ != 1234
    return 130;
#endif
#if __STDC_ENDIAN_BIG__ != 4321
    return 131;
#endif

    return 42;
}

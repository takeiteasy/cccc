// CCCC_FLAGS: --testing
// Case labels keep all 64 bits and an unsigned 64-bit switch is ordered as
// unsigned, in jump tables, compare trees and ranges.

#include <limits.h>

#pragma cccc suite begin "switch_wide"

int wide_labels(unsigned long c) {
    switch (c) {
        case 4294967296UL:
            return 2;
        case 9223372036854775808UL:
            return 4;
        default:
            return 0;
    }
}

int one_wide_label(long c) {
    switch (c) {
        case 1:
            return 1;
        case 0x100000001L:
            return 2;
        default:
            return 0;
    }
}

int extremes(long c) {
    switch (c) {
        case LONG_MIN:
            return 1;
        case LONG_MAX:
            return 2;
        case 0:
            return 3;
        default:
            return 0;
    }
}

int unsigned_range(unsigned long c) {
    switch (c) {
        case 0x7FFFFFFFFFFFFFFFUL ... 0x8000000000000001UL:
            return 1;
        default:
            return 0;
    }
}

int full_range(unsigned long c) {
    switch (c) {
        case 0 ... ULONG_MAX:
            return 1;
        default:
            return 0;
    }
}

int full_signed_range(long c) {
    switch (c) {
        case LONG_MIN ... LONG_MAX:
            return 1;
        default:
            return 0;
    }
}

int dense_high(unsigned long c) {
    switch (c) {
        case 0x8000000000000000UL:
            return 1;
        case 0x8000000000000001UL:
            return 2;
        case 0x8000000000000002UL:
            return 3;
        case 0x8000000000000003UL:
            return 4;
        case 0x8000000000000004UL:
            return 5;
        default:
            return 0;
    }
}

int dense_straddle(unsigned long c) {
    switch (c) {
        case 0x7FFFFFFFFFFFFFFEUL:
            return 1;
        case 0x7FFFFFFFFFFFFFFFUL:
            return 2;
        case 0x8000000000000000UL:
            return 3;
        case 0x8000000000000001UL:
            return 4;
        case 0x8000000000000002UL:
            return 5;
        default:
            return 0;
    }
}

int sparse_mixed(unsigned long c) {
    switch (c) {
        case 5UL:
            return 1;
        case 1000000UL:
            return 2;
        case 0x7FFFFFFFFFFFFFFFUL:
            return 3;
        case 0x8000000000000000UL:
            return 4;
        case 0xFFFFFFFFFFFFFFF0UL:
            return 5;
        case 0xFFFFFFFFFFFFFFFFUL:
            return 6;
        default:
            return 0;
    }
}

int uint_labels(unsigned c) {
    switch (c) {
        case 0xFFFFFFFFu:
            return 1;
        case 0x7FFFFFFFu ... 0x80000000u:
            return 2;
        default:
            return 0;
    }
}

int uint_full_range(unsigned c) {
    switch (c) {
        case 0 ... 0xFFFFFFFFu:
            return 1;
        default:
            return 0;
    }
}

int int_truncated_label(int c) {
    switch (c) {
        case 0x100000000L:
            return 1;
        default:
            return 0;
    }
}

enum Big : unsigned long { BIG_LOW = 1, BIG_HIGH = 0x8000000000000000UL };
enum Small : unsigned { SMALL_HIGH = 0xFFFFFFFFu };

int enum_big(enum Big e) {
    switch (e) {
        case BIG_LOW:
            return 1;
        case BIG_HIGH:
            return 2;
        default:
            return 0;
    }
}

int enum_small(enum Small e) {
    switch (e) {
        case SMALL_HIGH:
            return 1;
        default:
            return 0;
    }
}

int bitint40(unsigned _BitInt(40) c) {
    switch (c) {
        case 0xFFFFFFFFFFuwb:
            return 1;
        case 0x8000000000uwb:
            return 2;
        default:
            return 0;
    }
}

[[cccc::test(return = 42)]]
int test_wide_labels_distinct(void) {
    if (wide_labels(4294967296UL) != 2)
        return 1;
    if (wide_labels(9223372036854775808UL) != 4)
        return 2;
    if (wide_labels(0) != 0)
        return 3;
    return 42;
}

[[cccc::test(return = 42)]]
int test_one_wide_label_matches_only_itself(void) {
    if (one_wide_label(1) != 1)
        return 1;
    if (one_wide_label(0x100000001L) != 2)
        return 2;
    if (one_wide_label(0x200000001L) != 0)
        return 3;
    return 42;
}

[[cccc::test(return = 42)]]
int test_extreme_labels(void) {
    if (extremes(LONG_MIN) != 1 || extremes(LONG_MAX) != 2 || extremes(0) != 3)
        return 1;
    if (extremes(1) != 0 || extremes(-1) != 0)
        return 2;
    return 42;
}

[[cccc::test(return = 42)]]
int test_unsigned_ranges(void) {
    if (unsigned_range(0x7FFFFFFFFFFFFFFEUL) != 0)
        return 1;
    if (unsigned_range(0x7FFFFFFFFFFFFFFFUL) != 1 ||
        unsigned_range(0x8000000000000000UL) != 1 ||
        unsigned_range(0x8000000000000001UL) != 1)
        return 2;
    if (unsigned_range(0x8000000000000002UL) != 0)
        return 3;
    if (full_range(0) != 1 || full_range(ULONG_MAX) != 1 ||
        full_range(0x8000000000000000UL) != 1)
        return 4;
    if (full_signed_range(LONG_MIN) != 1 || full_signed_range(-1) != 1 ||
        full_signed_range(LONG_MAX) != 1)
        return 5;
    return 42;
}

[[cccc::test(return = 42)]]
int test_jump_table_above_2_63(void) {
    for (int i = 0; i < 5; i++)
        if (dense_high(0x8000000000000000UL + i) != i + 1)
            return 1;
    if (dense_high(0x7FFFFFFFFFFFFFFFUL) != 0 || dense_high(0) != 0 ||
        dense_high(0x8000000000000005UL) != 0)
        return 2;
    return 42;
}

[[cccc::test(return = 42)]]
int test_jump_table_straddling_2_63(void) {
    for (int i = 0; i < 5; i++)
        if (dense_straddle(0x7FFFFFFFFFFFFFFEUL + i) != i + 1)
            return 1;
    if (dense_straddle(0x7FFFFFFFFFFFFFFDUL) != 0 ||
        dense_straddle(0x8000000000000003UL) != 0 || dense_straddle(0) != 0)
        return 2;
    return 42;
}

[[cccc::test(return = 42)]]
int test_sparse_tree_mixed_signs(void) {
    if (sparse_mixed(5) != 1 || sparse_mixed(1000000) != 2 ||
        sparse_mixed(0x7FFFFFFFFFFFFFFFUL) != 3 ||
        sparse_mixed(0x8000000000000000UL) != 4 ||
        sparse_mixed(0xFFFFFFFFFFFFFFF0UL) != 5 ||
        sparse_mixed(0xFFFFFFFFFFFFFFFFUL) != 6)
        return 1;
    if (sparse_mixed(6) != 0 || sparse_mixed(0x8000000000000001UL) != 0 ||
        sparse_mixed(0xFFFFFFFFFFFFFFF1UL) != 0)
        return 2;
    return 42;
}

[[cccc::test(return = 42)]]
int test_unsigned_32_bit_labels(void) {
    if (uint_labels(0xFFFFFFFFu) != 1)
        return 1;
    if (uint_labels(0x7FFFFFFFu) != 2 || uint_labels(0x80000000u) != 2)
        return 2;
    if (uint_labels(0x80000001u) != 0 || uint_labels(0) != 0)
        return 3;
    if (uint_full_range(0) != 1 || uint_full_range(0xFFFFFFFFu) != 1 ||
        uint_full_range(0x80000000u) != 1)
        return 4;
    return 42;
}

[[cccc::test(return = 42)]]
int test_int_label_converts_to_condition_type(void) {
    if (int_truncated_label(0) != 1)
        return 1;
    if (int_truncated_label(1) != 0)
        return 2;
    return 42;
}

[[cccc::test(return = 42)]]
int test_enum_and_bitint_conditions(void) {
    if (enum_big(BIG_LOW) != 1 || enum_big(BIG_HIGH) != 2)
        return 1;
    if (enum_small(SMALL_HIGH) != 1)
        return 2;
    if (bitint40(0xFFFFFFFFFFuwb) != 1 || bitint40(0x8000000000uwb) != 2 ||
        bitint40(0) != 0)
        return 3;
    return 42;
}

#pragma cccc suite end

// CCCC_FLAGS: --testing
// Suite: per-lane `/` and `%` on unsigned vectors read their lanes unsigned.

#pragma cccc suite begin "vector_unsigned_divmod"

typedef unsigned long long u2l __attribute__((vector_size(16)));
typedef unsigned           u4 __attribute__((vector_size(16)));
typedef unsigned short     u8s __attribute__((vector_size(16)));
typedef unsigned char      u16c __attribute__((vector_size(16)));
typedef int                i4 __attribute__((vector_size(16)));

[[cccc::test(return = 42)]]
int test_u64_lanes(void) {
    u2l a = {0x8000000000000000ull, 100};
    u2l b = {2, 7};
    u2l d = a / b;
    u2l m = a % b;
    return d[0] == 0x4000000000000000ull && d[1] == 14 && m[0] == 0 && m[1] == 2
               ? 42
               : 1;
}

[[cccc::test(return = 42)]]
int test_u32_lanes(void) {
    u4 a = {0x80000000u, 10, 0xffffffffu, 1};
    u4 b = {2, 3, 0xffffffffu, 1};
    u4 d = a / b;
    u4 m = a % b;
    return d[0] == 0x40000000u && d[1] == 3 && d[2] == 1 && m[1] == 1 &&
                   m[2] == 0
               ? 42
               : 1;
}

[[cccc::test(return = 42)]]
int test_u16_lanes(void) {
    u8s a = {0x8000, 9, 0xffff, 1, 1, 1, 1, 1};
    u8s b = {2, 4, 0xffff, 1, 1, 1, 1, 1};
    u8s d = a / b;
    u8s m = a % b;
    return d[0] == 0x4000 && d[1] == 2 && d[2] == 1 && m[1] == 1 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_u8_lanes(void) {
    u16c a = {0x80, 9, 0xff, 200, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    u16c b = {2, 4, 0xff, 7, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    u16c d = a / b;
    u16c m = a % b;
    return d[0] == 0x40 && d[1] == 2 && d[2] == 1 && d[3] == 28 && m[3] == 4
               ? 42
               : 1;
}

[[cccc::test(return = 42)]]
int test_unsigned_left_operand_with_signed_right(void) {
    u4 a = {0x80000000u, 10, 1, 1};
    i4 b = {2, 3, 1, 1};
    u4 d = a / b;
    return d[0] == 0x40000000u && d[1] == 3 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_signed_lanes_stay_signed(void) {
    i4 a = {-8, -7, 7, 1};
    i4 b = {2, 2, -2, 1};
    i4 d = a / b;
    i4 m = a % b;
    return d[0] == -4 && d[1] == -3 && d[2] == -3 && m[1] == -1 ? 42 : 1;
}

#pragma cccc suite end

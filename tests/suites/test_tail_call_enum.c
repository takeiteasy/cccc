// CCCC_FLAGS: --testing
// A `return g(x)` of a wider value to an enum with a fixed underlying type
// narrows to that type, as a non-tail-call return does.

#include <stdint.h>

enum U8 : unsigned char { U8_A = 0, U8_B = 255 };
enum S8 : signed char { S8_A = -128, S8_B = 127 };
enum U16 : unsigned short { U16_A = 0, U16_B = 65535 };
enum S16 : short { S16_A = -32768, S16_B = 32767 };
enum U32 : unsigned { U32_A = 0, U32_B = 4294967295U };
enum S32 : int { S32_A = -2147483647 - 1, S32_B = 2147483647 };

long wide(long x) {
    return x;
}

enum U8  tail_u8(long x) { return wide(x); }
enum S8  tail_s8(long x) { return wide(x); }
enum U16 tail_u16(long x) { return wide(x); }
enum S16 tail_s16(long x) { return wide(x); }
enum U32 tail_u32(long x) { return wide(x); }
enum S32 tail_s32(long x) { return wide(x); }

[[cccc::test]]
void test_tail_call_enum_unsigned_char(void) {
    AssertEq((long)tail_u8(0x1FF), 0xFFL);
    AssertEq((long)tail_u8(0x100), 0L);
}

[[cccc::test]]
void test_tail_call_enum_signed_char(void) {
    AssertEq((long)tail_s8(0x17F), 127L);
    AssertEq((long)tail_s8(0x180), -128L);
}

[[cccc::test]]
void test_tail_call_enum_unsigned_short(void) {
    AssertEq((long)tail_u16(0x1FFFF), 0xFFFFL);
    AssertEq((long)tail_u16(0x10000), 0L);
}

[[cccc::test]]
void test_tail_call_enum_short(void) {
    AssertEq((long)tail_s16(0x17FFF), 32767L);
    AssertEq((long)tail_s16(0x18000), -32768L);
}

[[cccc::test]]
void test_tail_call_enum_unsigned(void) {
    AssertEq((long)tail_u32(0x1FFFFFFFFL), 4294967295L);
    AssertEq((long)tail_u32(0x100000000L), 0L);
}

[[cccc::test]]
void test_tail_call_enum_int(void) {
    AssertEq((long)tail_s32(0x17FFFFFFFL), 2147483647L);
    AssertEq((long)tail_s32(0x180000000L), -2147483647L - 1);
}

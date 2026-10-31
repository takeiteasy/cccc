// CCCC_FLAGS: --testing
// A direct call's result is taken as already normalised to the callee's return
// type, so the caller drops the extension after it. Every callee here returns a
// value narrowed from a wider one; a caller that assumed more than the callee
// guarantees would see the wide value.

#include <stdlib.h>

enum E32 : int { E32_MIN = -2147483647 - 1, E32_MAX = 2147483647 };

int ret_int(long x) {
    return (int)x;
}
unsigned ret_uint(long x) {
    return (unsigned)x;
}
signed char ret_schar(long x) {
    return (signed char)x;
}
unsigned char ret_uchar(long x) {
    return (unsigned char)x;
}
short ret_short(long x) {
    return (short)x;
}
unsigned short ret_ushort(long x) {
    return (unsigned short)x;
}
_Bool ret_bool(long x) {
    return x;
}
enum E32 ret_enum(long x) {
    return (enum E32)x;
}
long ret_long(long x) {
    return x;
}

int fwd(long x);

int rec(long n) {
    if (n <= 0)
        return (int)0x1FFFFFFFFL;
    int r = rec(n - 1);
    return r;
}

// The call result lands in a local that a loop keeps in a register.
#define KEEP(T, call)                                                          \
    long sum = 0;                                                              \
    for (int k = 0; k < 2; k++) {                                              \
        T v  = call;                                                           \
        sum += (long)v;                                                        \
    }                                                                          \
    return sum

long keep_int(long x) {
    KEEP(int, ret_int(x));
}
long keep_uint(long x) {
    KEEP(unsigned, ret_uint(x));
}
long keep_schar(long x) {
    KEEP(signed char, ret_schar(x));
}
long keep_uchar(long x) {
    KEEP(unsigned char, ret_uchar(x));
}
long keep_short(long x) {
    KEEP(short, ret_short(x));
}
long keep_ushort(long x) {
    KEEP(unsigned short, ret_ushort(x));
}
long keep_bool(long x) {
    KEEP(_Bool, ret_bool(x));
}
long keep_enum(long x) {
    KEEP(enum E32, ret_enum(x));
}
long keep_fwd(long x) {
    KEEP(int, fwd(x));
}

// A long result narrowed by the caller keeps its extension.
long narrow_long(long x) {
    KEEP(int, (int)ret_long(x));
}

// An indirect call is not a direct one.
long keep_indirect(long x) {
    int (*fp)(long) = ret_int;
    KEEP(int, fp(x));
}

// A host function's result is not normalised by anything in the guest.
long keep_abs(long x) {
    KEEP(int, abs((int)x));
}

int fwd(long x) {
    return (int)x;
}

[[cccc::test]]
void test_call_result_int(void) {
    AssertEq(keep_int(0x17FFFFFFFL), 2 * 2147483647L);
    AssertEq(keep_int(0x180000000L), 2 * (-2147483647L - 1));
}

[[cccc::test]]
void test_call_result_unsigned(void) {
    AssertEq(keep_uint(0x1FFFFFFFFL), 2 * 4294967295L);
    AssertEq(keep_uint(0x100000000L), 0L);
}

[[cccc::test]]
void test_call_result_char(void) {
    AssertEq(keep_schar(0x17F), 2 * 127L);
    AssertEq(keep_schar(0x180), 2 * -128L);
    AssertEq(keep_uchar(0x1FF), 2 * 255L);
    AssertEq(keep_uchar(0x100), 0L);
}

[[cccc::test]]
void test_call_result_short(void) {
    AssertEq(keep_short(0x17FFF), 2 * 32767L);
    AssertEq(keep_short(0x18000), 2 * -32768L);
    AssertEq(keep_ushort(0x1FFFF), 2 * 65535L);
    AssertEq(keep_ushort(0x10000), 0L);
}

[[cccc::test]]
void test_call_result_bool_enum(void) {
    AssertEq(keep_bool(0x100), 2L);
    AssertEq(keep_bool(0), 0L);
    AssertEq(keep_enum(0x17FFFFFFFL), 2 * 2147483647L);
    AssertEq(keep_enum(0x180000000L), 2 * (-2147483647L - 1));
}

[[cccc::test]]
void test_call_result_forward_declared(void) {
    AssertEq(keep_fwd(0x17FFFFFFFL), 2 * 2147483647L);
    AssertEq(keep_fwd(0x180000000L), 2 * (-2147483647L - 1));
}

[[cccc::test]]
void test_call_result_recursive(void) {
    AssertEq(rec(5), -1);
}

[[cccc::test]]
void test_call_result_kept_extension(void) {
    AssertEq(narrow_long(0x17FFFFFFFL), 2 * 2147483647L);
    AssertEq(narrow_long(0x180000000L), 2 * (-2147483647L - 1));
    AssertEq(keep_indirect(0x180000000L), 2 * (-2147483647L - 1));
    AssertEq(keep_abs(-5), 10L);
}

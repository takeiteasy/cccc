// CCCC_FLAGS: --testing
// CCCC_NATIVE_SKIP: the host libm has no __sqrtf16 family (macOS 15+ only)
// _Float16 arguments and results through registered host functions: the
// half-precision math functions the macOS SDK's <math.h> declares.

#include <string.h>

extern _Float16 __sqrtf16(_Float16);
extern _Float16 __fabsf16(_Float16);
extern _Float16 __fmaf16(_Float16, _Float16, _Float16);
extern _Float16 __hypotf16(_Float16, _Float16);
extern _Float16 __nextafterf16(_Float16, _Float16);
extern _Float16 __copysignf16(_Float16, _Float16);

static unsigned bits(_Float16 h) {
    unsigned short b;
    memcpy(&b, &h, sizeof b);
    return b;
}

[[cccc::test(return = 42)]]
int test_one_argument(void) {
    if ((double)__sqrtf16(16) != 4)
        return 1;
    if ((double)__fabsf16(-2.5f16) != 2.5)
        return 2;
    if (bits(__sqrtf16(2)) != 0x3da8)
        return 3;
    return 42;
}

[[cccc::test(return = 42)]]
int test_several_arguments(void) {
    if (bits(__fmaf16(16, 2.5f16, -3.25f16)) != 0x5098)
        return 1;
    if (bits(__hypotf16(2.5f16, -3.25f16)) != 0x441a)
        return 2;
    if (bits(__copysignf16(2.5f16, -3.25f16)) != 0xc100)
        return 3;
    return 42;
}

[[cccc::test(return = 42)]]
int test_nested_calls(void) {
    _Float16 h = __sqrtf16(__fmaf16(__sqrtf16(16), __fabsf16(-4), 0));
    if ((double)h != 4)
        return 1;
    if (bits(__nextafterf16(__sqrtf16(0), -1)) != 0x8001)
        return 2;
    return 42;
}

[[cccc::test(return = 42)]]
int test_integer_and_double_arguments(void) {
    int    n = 9;
    double d = 25.0;
    if ((int)__sqrtf16((_Float16)n) != 3)
        return 1;
    if ((int)__sqrtf16((_Float16)d) != 5)
        return 2;
    __sqrtf16(4); // result discarded
    return 42;
}

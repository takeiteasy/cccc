// CCCC_FLAGS: --testing
// Predefined target macros agree with the bundled headers and type model.
#include <float.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define SA(x)           _Static_assert(x, #x)
#define SAME_TYPE(a, b) _Generic((a), typeof(b): 1, default: 0)

SA(__CHAR_BIT__ == CHAR_BIT);
SA(__SCHAR_MAX__ == SCHAR_MAX);
SA(__SHRT_MAX__ == SHRT_MAX);
SA(__INT_MAX__ == INT_MAX);
SA(__LONG_MAX__ == LONG_MAX);
SA(__LONG_LONG_MAX__ == LLONG_MAX);
SA(__INT8_MAX__ == INT8_MAX);
SA(__UINT8_MAX__ == UINT8_MAX);
SA(__INT16_MAX__ == INT16_MAX);
SA(__UINT16_MAX__ == UINT16_MAX);
SA(__INT32_MAX__ == INT32_MAX);
SA(__UINT32_MAX__ == UINT32_MAX);
SA(__INT64_MAX__ == INT64_MAX);
SA(__UINT64_MAX__ == UINT64_MAX);
SA(__INTMAX_MAX__ == INTMAX_MAX);
SA(__UINTMAX_MAX__ == UINTMAX_MAX);
SA(__INTPTR_MAX__ == INTPTR_MAX);
SA(__UINTPTR_MAX__ == UINTPTR_MAX);
SA(__PTRDIFF_MAX__ == PTRDIFF_MAX);
SA(__SIZE_MAX__ == SIZE_MAX);
SA(__SIG_ATOMIC_MAX__ == SIG_ATOMIC_MAX);
SA(__SIG_ATOMIC_MIN__ == SIG_ATOMIC_MIN);
SA(__WCHAR_MAX__ == WCHAR_MAX);
SA(__WCHAR_MIN__ == WCHAR_MIN);
SA(__WINT_MAX__ == WINT_MAX);
SA(__WINT_MIN__ == WINT_MIN);

SA(__INT_WIDTH__ == sizeof(int) * CHAR_BIT);
SA(__LONG_WIDTH__ == sizeof(long) * CHAR_BIT);
SA(__LLONG_WIDTH__ == sizeof(long long) * CHAR_BIT);
SA(__POINTER_WIDTH__ == sizeof(void *) * CHAR_BIT);
SA(__SIZE_WIDTH__ == sizeof(size_t) * CHAR_BIT);
SA(__WCHAR_WIDTH__ == sizeof(wchar_t) * CHAR_BIT);
SA(__SIZEOF_WCHAR_T__ == sizeof(wchar_t));
SA(__SIZEOF_WINT_T__ == sizeof(wint_t));
SA(_LP64 == 1);
SA(__BITINT_MAXWIDTH__ == BITINT_MAXWIDTH);

SA(SAME_TYPE((__INT8_TYPE__)0, int8_t));
SA(SAME_TYPE((__UINT8_TYPE__)0, uint8_t));
SA(SAME_TYPE((__INT16_TYPE__)0, int16_t));
SA(SAME_TYPE((__UINT16_TYPE__)0, uint16_t));
SA(SAME_TYPE((__INT32_TYPE__)0, int32_t));
SA(SAME_TYPE((__UINT32_TYPE__)0, uint32_t));
SA(SAME_TYPE((__INT64_TYPE__)0, int64_t));
SA(SAME_TYPE((__UINT64_TYPE__)0, uint64_t));
SA(SAME_TYPE((__INTMAX_TYPE__)0, intmax_t));
SA(SAME_TYPE((__UINTMAX_TYPE__)0, uintmax_t));
SA(SAME_TYPE((__INTPTR_TYPE__)0, intptr_t));
SA(SAME_TYPE((__UINTPTR_TYPE__)0, uintptr_t));
SA(SAME_TYPE((__PTRDIFF_TYPE__)0, ptrdiff_t));
SA(SAME_TYPE((__SIZE_TYPE__)0, size_t));
SA(SAME_TYPE((__WCHAR_TYPE__)0, wchar_t));
SA(SAME_TYPE((__WINT_TYPE__)0, wint_t));
SA(SAME_TYPE((__CHAR16_TYPE__)0, unsigned short));
SA(SAME_TYPE((__CHAR32_TYPE__)0, unsigned int));

SA(__FLT_RADIX__ == FLT_RADIX);
SA(__FLT_MANT_DIG__ == FLT_MANT_DIG);
SA(__DBL_MANT_DIG__ == DBL_MANT_DIG);
SA(__LDBL_MANT_DIG__ == LDBL_MANT_DIG);
SA(__FLT_DIG__ == FLT_DIG);
SA(__DBL_DIG__ == DBL_DIG);
SA(__LDBL_DIG__ == LDBL_DIG);
SA(__FLT_MIN_EXP__ == FLT_MIN_EXP);
SA(__DBL_MAX_EXP__ == DBL_MAX_EXP);
SA(__LDBL_MAX_10_EXP__ == LDBL_MAX_10_EXP);
SA(__FLT_MAX__ == FLT_MAX);
SA(__DBL_MAX__ == DBL_MAX);
SA(__LDBL_MAX__ == LDBL_MAX);
SA(__FLT_MIN__ == FLT_MIN);
SA(__DBL_MIN__ == DBL_MIN);
SA(__FLT_EPSILON__ == FLT_EPSILON);
SA(__DBL_EPSILON__ == DBL_EPSILON);
SA(__LDBL_EPSILON__ == LDBL_EPSILON);
SA(__DBL_DENORM_MIN__ == DBL_TRUE_MIN);
SA(__DECIMAL_DIG__ == DECIMAL_DIG);

SA(__ORDER_LITTLE_ENDIAN__ == 1234);
SA(__ORDER_BIG_ENDIAN__ == 4321);

// Features cccc lacks, and identities it does not claim, stay undefined.
#if defined(__clang__) || defined(__STDC_NO_THREADS__) ||                      \
    defined(__ARM_NEON) || defined(__BLOCKS__)
#error "predefined macro advertises a feature cccc does not have"
#endif

#ifdef __APPLE__
#ifndef __MACH__
#error "__MACH__ missing on Apple"
#endif
#ifndef __APPLE_CC__
#error "__APPLE_CC__ missing on Apple"
#endif
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ <= 0
#error "bad __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__"
#endif
#endif

[[cccc::test(return = 42)]]
int test_byte_order(void) {
    uint32_t      one = 1;
    unsigned char first;
    memcpy(&first, &one, 1);
    if ((__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__) != (first == 1))
        return 1;
    return 42;
}

[[cccc::test(return = 42)]]
int test_format_macros(void) {
    char buf[64];
    snprintf(buf, sizeof buf, "%" __INT64_FMTd__, INT64_MIN);
    if (strcmp(buf, "-9223372036854775808"))
        return 1;
    snprintf(buf, sizeof buf, "%" __SIZE_FMTx__, (size_t)255);
    if (strcmp(buf, "ff"))
        return 2;
    snprintf(buf, sizeof buf, "%" __UINT8_FMTu__, (unsigned char)200);
    if (strcmp(buf, "200"))
        return 3;
    return 42;
}

[[cccc::test(return = 42)]]
int test_limits_values(void) {
    if (__INT64_MAX__ != INT64_MAX || __UINT32_MAX__ != 4294967295U)
        return 1;
    if (sizeof(__UINT8_MAX__) < 1)
        return 2;
    return 42;
}

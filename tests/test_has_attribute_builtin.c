int main(void) {
    int result = 0;

#if __has_attribute(aligned)
    result += 4;
#endif
#if __has_attribute(__unused__)
    result += 4;
#endif
#if __has_attribute(deprecated)
    result += 4;
#endif
#if __has_attribute(comptime)
    result += 4;
#endif
#if __has_attribute(format)
    result += 4;
#endif
#if __has_attribute(noreturn)
    result += 4;
#endif

#if __has_builtin(__builtin_mul_overflow)
    result += 4;
#endif
#if __has_builtin(__builtin_alloca)
    result += 4;
#endif
#if __has_builtin(__builtin_offsetof)
    return 2;
#endif

#if __has_c_attribute(maybe_unused)
    result += 4;
#endif
#if __has_c_attribute(deprecated)
    result += 4;
#endif
#if __has_c_attribute(cccc::comptime)
    result += 4;
#endif
#if __has_c_attribute(noreturn)
    result += 4;
#endif
#if __has_c_attribute(fallthrough)
    result += 4;
#endif
#if __has_c_attribute(nodiscard)
    result += 4;
#endif
#if __has_cpp_attribute(deprecated)
    return 4;
#endif

    // Verify C23 attributes return date value (202311L), not just 1
#if __has_c_attribute(deprecated) != 202311L
    return 5;
#endif
#if __has_c_attribute(maybe_unused) != 202311L
    return 6;
#endif
#if __has_c_attribute(noreturn) != 202311L
    return 7;
#endif
    // CCCC vendor attrs return 1 (not a date)
#if __has_c_attribute(cccc::comptime) != 1
    return 8;
#endif

    // 14 checks × 4 = 56; subtract 14 to get exit code 42
    return result - 14;
}

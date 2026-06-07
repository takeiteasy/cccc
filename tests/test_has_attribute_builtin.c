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
#if __has_attribute(comptime) && __has_attribute(macro)
    result += 4;
#endif
#if __has_attribute(format) || __has_attribute(noreturn)
    return 1;
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
#if __has_c_attribute(jcc::comptime)
    result += 4;
#endif
#if __has_c_attribute(macro, jcc)
    result += 4;
#endif
#if __has_c_attribute(fallthrough) || __has_c_attribute(nodiscard)
    return 3;
#endif
#if __has_cpp_attribute(deprecated)
    return 4;
#endif

    return result + 2;
}

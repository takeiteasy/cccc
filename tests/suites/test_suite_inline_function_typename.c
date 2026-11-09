// CCCC_FLAGS: --testing
// A function type-name written inline as an operand (no typedef).

[[cccc::test(return = 42)]]
int test_inline_function_typename(void) {
    if (!__builtin_types_compatible_p(int(void), int(void)))
        return 1;
    if (__builtin_types_compatible_p(int(void), long(void)))
        return 2;
    if (!__builtin_types_compatible_p(void(int), void(int)))
        return 3;
    if (__builtin_types_compatible_p(void(int), void(long)))
        return 4;
    if (!__builtin_types_compatible_p(int(int, ...), int(int, ...)))
        return 5;
    if (__builtin_types_compatible_p(int(void), int))
        return 6;
    if (!__builtin_types_compatible_p(int (*)(void), int (*)(void)))
        return 7;
    if (sizeof(int (*)(void)) != sizeof(void *))
        return 8;
    return 42;
}

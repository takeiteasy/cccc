// CCCC_FLAGS: --testing
// CCCC_NATIVE_SKIP: negative (error=) tests
// Suite: a bare function template name needs a target type that deduces it
// exactly.

#include <stdio.h>

#define OVL __attribute__((overloadable))

@template(T) static T twice(T x) {
    return x + x;
}
@template(T) static int constp(const T *p) {
    return 0;
}
@template(T) static T mixed(T a, long b) {
    return a;
}
@template(T) static int fixed1(T a) {
    return 0;
}
@template(T) static int vararg(T a, ...) {
    return 0;
}
@template(T) static T apply(T (*f)(T), T x) {
    return f(x);
}

[[cccc::test(error = "invalid operands to +")]]
void test_arithmetic(void) {
    int x = (int)(twice + 1);
}

[[cccc::test(error = "'twice' is a template")]]
void test_void_cast(void) {
    (void)twice;
}

[[cccc::test(error = "'twice' is a template")]]
void test_condition(void) {
    if (twice)
        return;
}

[[cccc::test(error = "'twice' is a template")]]
void test_integer_target(void) {
    int x = twice;
}

[[cccc::test(error = "'twice' is a template")]]
void test_void_pointer_target(void) {
    void *p = twice;
}

[[cccc::test(error = "'twice' is a template")]]
void test_void_pointer_cast(void) {
    void *p = (void *)twice;
}

[[cccc::test(error = "'twice' is a template")]]
void test_sizeof(void) {
    unsigned long n = sizeof twice;
}

[[cccc::test(error = "'twice' is a template")]]
void test_variadic_argument(void) {
    printf("%p", twice);
}

[[cccc::test(error = "no overloaded function 'constp' matches the target type")]]
void test_no_added_qualifier(void) {
    int (*p)(int *) = constp;
}

[[cccc::test(error = "no overloaded function 'mixed' matches the target type")]]
void test_fixed_parameter_differs(void) {
    int (*p)(int, int) = mixed;
}

[[cccc::test(error = "no overloaded function 'fixed1' matches the target type")]]
void test_parameter_count(void) {
    int (*p)(int, int) = fixed1;
}

[[cccc::test(error = "no overloaded function 'vararg' matches the target type")]]
void test_variadic_mismatch(void) {
    int (*p)(int) = vararg;
}

[[cccc::test(error = "no overloaded function 'twice' matches the target type")]]
void test_return_mismatch(void) {
    int (*p)(double) = twice;
}

[[cccc::test(error = "this type cannot be a template argument")]]
void test_template_argument_is_not_deduced(void) {
    apply(twice, 21);
}

// A set of one function and a template has no target-less use.
static int OVL one(int x) {
    return 1;
}
@template(T) static int one(T x) {
    return 2;
}

[[cccc::test(error = "no overloaded function 'one' matches the target type")]]
void test_one_function_and_template_untargeted(void) {
    void *p = one;
}

[[cccc::test(error = "candidate template: one")]]
void test_no_match_lists_template(void) {
    int (*p)(void) = one;
}

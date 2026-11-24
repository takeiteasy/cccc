// CCCC_FLAGS: --testing
// Suite: enable_if and diagnose_if conditions, evaluated with each call's
// arguments.

#define OVL __attribute__((overloadable))

// clang-format off
__attribute__((overloadable))
__attribute__((enable_if(n > 0, "positive")))
static int sign(int n) {
    return 1;
}
// clang-format on
static int OVL sign(int n) {
    return 0;
}

[[cccc::test]]
void test_constant_argument_enables(void) {
    int runtime = 5;
    AssertEq(sign(3), 1);
    AssertEq(sign(-3), 0);
    AssertEq(sign(runtime), 0);
}

// clang-format off
__attribute__((overloadable))
__attribute__((enable_if(1, "one")))
static int mode(void) {
    return 1;
}
// clang-format on

// clang-format off
__attribute__((overloadable))
__attribute__((enable_if(1, "one")))
__attribute__((enable_if(1, "two")))
static int mode(void) {
    return 2;
}
// clang-format on

[[cccc::test]]
void test_longer_enable_if_list_wins(void) {
    AssertEq(mode(), 2);
}

// clang-format off
__attribute__((enable_if(n < 10, "n must be below 10")))
static int small(int n) {
    return n;
}
// clang-format on

[[cccc::test]]
void test_plain_function(void) {
    int runtime = 20;
    AssertEq(small(4), 4);
    if (0)
        small(runtime);
}

// clang-format off
__attribute__((overloadable))
__attribute__((enable_if(a + b == 3 && !b, "")))
static int pick(int a, int b) {
    return 1;
}
// clang-format on
static int OVL pick(long a, int b) {
    return 2;
}

[[cccc::test]]
void test_operators_in_condition(void) {
    AssertEq(pick(3, 0), 1);
    AssertEq(pick(2, 1), 2);
}

// clang-format off
__attribute__((overloadable))
__attribute__((enable_if(__builtin_constant_p(n) && n > 100, "big")))
static int scale(int n) {
    return 1;
}
// clang-format on
static int OVL scale(int n) {
    return 0;
}

[[cccc::test]]
void test_builtin_constant_p_of_argument(void) {
    int runtime = 500;
    AssertEq(scale(500), 1);
    AssertEq(scale(runtime), 0);
}

// clang-format off
__attribute__((overloadable))
__attribute__((enable_if(n <= __builtin_object_size(dst, 0), "fits")))
static int fill(char *const dst, int n) {
    return 1;
}
// clang-format on
static int OVL fill(char *const dst, int n) {
    return 0;
}

[[cccc::test]]
void test_object_size_of_argument(void) {
    char buf[8];
    AssertEq(fill(buf, 8), 1);
    AssertEq(fill(buf, 9), 0);
}

static int always(int n) __attribute__((enable_if(1, "always"))) {
    return n;
}

[[cccc::test]]
void test_address_of_always_enabled(void) {
    int (*fp)(int) = always;
    AssertEq(fp(7), 7);
}

static int checked_div(int d)
    __attribute__((diagnose_if(d == 0, "division by zero", "warning"))) {
    return 10 / (d ? d : 1);
}

[[cccc::test]]
void test_diagnose_if_does_not_change_the_call(void) {
    AssertEq(checked_div(5), 2);
}

[[cccc::test]]
void test_feature_tests(void) {
#if __has_attribute(enable_if) && __has_attribute(diagnose_if) &&              \
    __has_attribute(__enable_if__)
    AssertTrue(1);
#else
    AssertFail();
#endif
#if __has_c_attribute(clang::enable_if) || __has_c_attribute(clang::diagnose_if)
    AssertFail();
#endif
}

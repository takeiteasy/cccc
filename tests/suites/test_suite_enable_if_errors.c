// CCCC_FLAGS: --testing
// CCCC_NATIVE_SKIP: negative (error=) tests
// Suite: enable_if and diagnose_if calls that are rejected.

// clang-format off
__attribute__((enable_if(n < 10, "n must be below 10")))
static int small(int n) {
    return n;
}
// clang-format on

[[cccc::test(error = "candidate disabled: n must be below 10")]]
void test_plain_function_disabled(void) {
    small(20);
}

[[cccc::test(error = "which has an enable_if that is not always true")]]
void test_address_of_conditional_function(void) {
    int (*fp)(int) = small;
}

// clang-format off
__attribute__((overloadable))
__attribute__((enable_if(n > 0, "positive only")))
static int sign(int n) {
    return 1;
}
// clang-format on

// clang-format off
__attribute__((overloadable))
__attribute__((enable_if(n < -5, "very negative")))
static int sign(int n) {
    return 2;
}
// clang-format on

[[cccc::test(error = "disabled: positive only")]]
void test_every_overload_disabled(void) {
    sign(-1);
}

// clang-format off
__attribute__((overloadable))
__attribute__((enable_if(i, "")))
static int g(int i, int j) {
    return 1;
}
// clang-format on

// clang-format off
__attribute__((overloadable))
__attribute__((enable_if(j, "")))
__attribute__((enable_if(1, "")))
static int g(int i, int j) {
    return 2;
}
// clang-format on

[[cccc::test(error = "call to 'g' is ambiguous")]]
void test_unrelated_enable_ifs_are_ambiguous(void) {
    g(1, 1);
}

static int positive(int x)
    __attribute__((diagnose_if(x < 0, "x must not be negative", "error"))) {
    return x;
}

[[cccc::test(error = "x must not be negative")]]
void test_diagnose_if_error(void) {
    positive(-1);
}

[[cccc::test(error = "diagnose_if's severity must be")]]
void test_diagnose_if_bad_severity(void) {
    extern int bad(int x) __attribute__((diagnose_if(x, "message", "fatal")));
}

[[cccc::test(error = "undefined variable 'm'")]]
void test_condition_names_unknown(void) {
    extern int unknown(int n) __attribute__((enable_if(m > 0, "")));
}

// Tests for return value assertions: string, float, and operator forms (tickets #346, #343).
// CCCC_FLAGS: --testing

#pragma cccc suite begin "return_types"

// Regression: return = -1 must work (latent #342 bug with signed numbers).
[[cccc::test(return = -1)]]
int test_return_neg_one(void) {
    return -1;
}

// Float return value equality.
[[cccc::test(return = 3.14)]]
double test_return_float(void) {
    return 3.14;
}

// Float return: computed value.
[[cccc::test(return = 2.0)]]
double test_return_float_computed(void) {
    double x = 1.0;
    return x + 1.0;
}

// String return equality via strcmp.
[[cccc::test(return = "hello")]]
const char *test_return_str(void) {
    return "hello";
}

// String return: different value.
[[cccc::test(return = "world")]]
const char *test_return_str_world(void) {
    return "world";
}

// Char return via int path (char is int-width in C).
[[cccc::test(return = 65)]]
int test_return_char_as_int(void) {
    return (int)'A';
}

#pragma cccc suite end

// Comparison operators on integer return values.
#pragma cccc suite begin "return_ops"

[[cccc::test(return > 0)]]
int test_return_gt_zero(void) {
    return 1;
}

[[cccc::test(return >= 1)]]
int test_return_ge_one(void) {
    return 1;
}

[[cccc::test(return < 10)]]
int test_return_lt_ten(void) {
    return 5;
}

[[cccc::test(return <= 5)]]
int test_return_le_five(void) {
    return 5;
}

[[cccc::test(return != 0)]]
int test_return_ne_zero(void) {
    return 42;
}

#pragma cccc suite end

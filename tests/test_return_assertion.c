// Tests for [[cccc::test(return = N)]] — return value assertion (ticket #342).
// CCCC_FLAGS: --testing

#pragma cccc suite begin "return_assertion"

// Basic: return = 0 passes when function returns 0.
[[cccc::test(return = 0)]]
int test_return_zero(void) {
    return 0;
}

// Basic: return = 1 passes when function returns 1.
[[cccc::test(return = 1)]]
int test_return_one(void) {
    return 1;
}

// Larger return value.
[[cccc::test(return = 42)]]
int test_return_forty_two(void) {
    return 42;
}

// Negative return value.
[[cccc::test(return = -1)]]
int test_return_negative(void) {
    return -1;
}

// Return value computed, not a literal.
[[cccc::test(return = 6)]]
int test_return_computed(void) {
    int x = 2;
    int y = 3;
    return x * y;
}

// Combined with name = "...".
[[cccc::test(return = 7, name = "addition returns correct sum")]]
int test_return_named(void) {
    return 3 + 4;
}

#pragma cccc suite end

// Combined with suite = "..." in attribute (outside pragma block).
[[cccc::test(return = 100, suite = "return_combined")]]
int test_return_with_suite(void) {
    return 100;
}

// return = combined with $assert — both must pass.
[[cccc::test(return = 1, suite = "return_combined")]]
int test_return_with_assert(void) {
    $assert_eq(1 + 1, 2);
    return 1;
}

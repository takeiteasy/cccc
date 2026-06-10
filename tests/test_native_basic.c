// Native mode basic assertions test.
// CCCC_FLAGS: --testing

[[cccc::test(mode = "native")]]
void test_native_eq(void) {
    $assert_eq(1 + 1, 2);
    $assert_eq(6 * 7, 42);
}

[[cccc::test(mode = "native")]]
void test_native_neq(void) {
    $assert_neq(1, 2);
}

[[cccc::test(mode = "native")]]
void test_native_bool(void) {
    $assert(1 == 1);
    $assert_false(1 == 2);
}

[[cccc::test(mode = "native")]]
void test_native_cmp(void) {
    $assert_gt(10, 5);
    $assert_lt(3, 7);
    $assert_ge(5, 5);
    $assert_le(4, 4);
}

[[cccc::test(mode = "native")]]
void test_native_null(void) {
    void *p = 0;
    $assert_null(p);
    int x = 42;
    $assert_not_null(&x);
}

[[cccc::test(mode = "native")]]
void test_native_string(void) {
    $assert_streq("hello", "hello");
    $assert_streq_len("abc123", "abcXXX", 3);
}

[[cccc::test(mode = "native")]]
void test_native_float(void) {
    $assert_float_within(0.01, 3.14, 3.14159);
    $assert_double_eq(2.0, 1.0 + 1.0);
}

[[cccc::test(mode = "native")]]
void test_native_bits(void) {
    $assert_bit_high(0, 0xFF);
    $assert_bit_low(7, 0x0F);
    $assert_bits(0xF0, 0xA0, 0xAB);
}

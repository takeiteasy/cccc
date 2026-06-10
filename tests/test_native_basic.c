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

int g_counter = 0;

[[cccc::test(mode = "native")]]
void test_native_incdec(void) {
    int local = 0;
    local++;
    ++local;
    $assert_eq(local, 2);

    local--;
    --local;
    $assert_eq(local, 0);

    g_counter++;
    ++g_counter;
    $assert_eq(g_counter, 2);

    g_counter -= 1;
    $assert_eq(g_counter, 1);
}

[[cccc::test(mode = "native")]]
void test_native_anon_struct(void) {
    struct { int x; int y; } pt;
    pt.x = 3;
    pt.y = 4;
    $assert_eq(pt.x + pt.y, 7);

    union { int i; float f; } u;
    u.i = 42;
    $assert_eq(u.i, 42);
}

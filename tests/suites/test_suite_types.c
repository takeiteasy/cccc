// CCCC_FLAGS: --testing
// Consolidated suite: type sizes, char, casts, sizeof
// Source tests: test_char_as_int, test_int_size, test_long_size,
//               test_char_return, test_cast_expressions, test_sizeof

#pragma cccc suite begin "types"

// test_int_size
[[cccc::test]]
void test_sizeof_int(void) {
    int x;
    AssertEq((int)sizeof(x), 4);
}

// test_long_size
[[cccc::test]]
void test_sizeof_long(void) {
    long x;
    AssertEq((int)sizeof(x), 8);
}

// test_sizeof
[[cccc::test]]
void test_sizeof_various(void) {
    AssertEq((int)sizeof(char),  1);
    AssertEq((int)sizeof(int),   4);
    AssertEq((int)sizeof(long),  8);
    AssertEq((int)sizeof(int*),  8);
    AssertEq((int)sizeof(char*), 8);
    int arr[5];
    AssertEq((int)sizeof(arr), 20);
    struct Point { int x; int y; };
    AssertEq((int)sizeof(struct Point), 8);
    int x;
    AssertEq((int)sizeof(x), 4);
    char c;
    AssertEq((int)sizeof(c), 1);
    AssertEq((int)sizeof(x + 1),   4);
    AssertEq((int)sizeof(arr[0]),   4);
}

// test_char_as_int: char truncation of 1000 → -24 or 232 (impl-defined sign)
[[cccc::test]]
void test_char_truncation(void) {
    int i = 1000;
    char c = (char)i;
    int c_as_int = c;
    Assert(c_as_int == 232 || c_as_int == -24);
}

// test_char_return: signed char -24 path
[[cccc::test]]
void test_char_return(void) {
    int i = 1000;
    char c = (char)i;
    int c_as_int = c;
    AssertEq(c_as_int, -24);
}

// test_cast_expressions: helper functions retained as statics
static void use_params(int a, int b) { (void)a; (void)b; }

static int cast_void_casts(void) {
    int x = 10, y = 20;
    (void)x; (void)(x + y); (void)(x * 2);
    return 42;
}
static int cast_numeric_casts(void) {
    int x = 10;
    double d = 3.14;
    (int)d; (double)x; (long)x; (char)x;
    int result = (int)d + x;
    result += (int)(d * 2);
    return result;    // 19
}
static int cast_pointer_casts(void) {
    int x = 42;
    int *p = &x;
    (void *)p; (char *)p; (long *)p;
    int value = *(int *)(void *)p;
    return value;     // 42
}
static int cast_with_side_effects(void) {
    int x = 5;
    (void)(x++);
    (void)(x += 10);
    return x;         // 16
}
static int identity_fn(int x) { return x; }
static int cast_in_call(void) {
    double d = 7.9;
    return identity_fn((int)d);  // 7
}

[[cccc::test]]
void test_cast_expressions(void) {
    use_params(1, 2);
    int result = 0;
    result += cast_void_casts();       // 42
    result += cast_numeric_casts();    // 19 → 61
    result += cast_pointer_casts();    // 42 → 103
    result += cast_with_side_effects(); // 16 → 119
    result += cast_in_call();          // 7  → 126
    AssertEq(result, 126);
}

#pragma cccc suite end

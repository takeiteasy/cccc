// CCCC_FLAGS: --testing
// CCCC_EXPECT_STDOUT: not ok 1 - test_scalar_field
// CCCC_EXPECT_STDOUT: not ok 2 - test_array_element
// CCCC_EXPECT_STDOUT: not ok 3 - test_char_array
// CCCC_EXPECT_STDOUT: not ok 4 - test_string_pointer
// CCCC_EXPECT_STDOUT: not ok 5 - test_float_field
// CCCC_EXPECT_STDOUT: not ok 6 - test_nested_struct
// CCCC_EXPECT_STDOUT: not ok 7 - test_struct_against_scalar
// CCCC_EXPECT_STDOUT: not ok 8 - test_whole_value_equal
// CCCC_EXPECT_STDOUT: not ok 9 - test_late_array_element
// CCCC_EXPECT_STDOUT: not ok 10 - test_anonymous_member
// CCCC_EXPECT_STDOUT: not ok 11 - test_union_arm
// CCCC_EXPECT_STDOUT: not ok 12 - test_bit_field
// CCCC_EXPECT_STDOUT: not ok 13 - test_long_double
// CCCC_EXPECT_STDERR: expected return value = \(struct N\)\{\.a = 2\}, got \{\.a = 1, \.in = \{\.p = 0, \.c = 0\}, \.arr = \{0, 0, 0, 0\}, \.name = \{0, 0, 0, 0, 0, 0\}, \.s = 0, \.f = 0\}
// CCCC_EXPECT_STDERR: expected return value = \(struct N\)\{\.arr = \{1, 2, 9\}\}, got .*\.arr = \{1, 2, 3, 0\}
// CCCC_EXPECT_STDERR: expected return value = \(struct N\)\{\.name = "he"\}, got .*\.name = "hey"
// CCCC_EXPECT_STDERR: expected return value = \(struct N\)\{\.s = "abc"\}, got .*\.s = "abd"
// CCCC_EXPECT_STDERR: expected return value = \(struct N\)\{\.f = 1\.0\}, got .*\.f = 1\.25
// CCCC_EXPECT_STDERR: expected return value = \(struct N\)\{\.in = \{\.p = 1\}\}, got .*\.in = \{\.p = 2, \.c = 0\}
// CCCC_EXPECT_STDERR: expected return value = \(struct N\)\{\.in = 5\}, got .*\.in = \(type mismatch\)
// CCCC_EXPECT_STDERR: expected return value != \(struct N\)\{\.a = 1\}, got \{\.a = 1,
// CCCC_EXPECT_STDERR: expected return value = \(struct A\)\{0\}, got \{\.v = \{0, 0, 0, 0, 0, 0, 0, 0, 5\}\}
// CCCC_EXPECT_STDERR: expected return value = \(struct Anon\)\{\.ax = 1\}, got \{\.ax = 2\}
// CCCC_EXPECT_STDERR: expected return value = \(union U\)\{\.i = 5\}, got \{\.i = 6\}
// CCCC_EXPECT_STDERR: expected return value = \(struct Bits\)\{\.a = -2, \.b = 9, \.c = 7\}, got \{\.a = -2, \.b = 8, \.c = 7\}
// CCCC_EXPECT_STDERR: expected return value = \(struct Wide\)\{\.x = 1\.5, \.y = 3\}, got \{\.x = 2\.5, \.y = 3\}

struct In {
    int  p;
    char c;
};

struct N {
    int         a;
    struct In   in;
    int         arr[4];
    char        name[6];
    const char *s;
    float       f;
};

struct A {
    int v[9];
};

struct Anon {
    struct {
        int ax;
    };
};

union U {
    int   i;
    float f;
};

struct Bits {
    int      a : 3;
    unsigned b : 5;
    int      c;
};

struct Wide {
    long double x;
    int         y;
};

[[cccc::test(return = (struct N){.a = 2})]] struct N test_scalar_field(void) {
    return (struct N){.a = 1};
}

[[cccc::test(return = (struct N){.arr = {1, 2, 9}})]] struct N
    test_array_element(void) {
    return (struct N){.arr = {1, 2, 3}};
}

[[cccc::test(return = (struct N){.name = "he"})]] struct N
    test_char_array(void) {
    return (struct N){.name = "hey"};
}

[[cccc::test(return = (struct N){.s = "abc"})]] struct N
    test_string_pointer(void) {
    return (struct N){.s = "abd"};
}

[[cccc::test(return = (struct N){.f = 1.0})]] struct N test_float_field(void) {
    return (struct N){.f = 1.25f};
}

[[cccc::test(return = (struct N){.in = {.p = 1}})]] struct N
    test_nested_struct(void) {
    return (struct N){.in = {2, 0}};
}

[[cccc::test(return = (struct N){.in = 5})]] struct N
    test_struct_against_scalar(void) {
    return (struct N){0};
}

[[cccc::test(return != (struct N){.a = 1})]] struct N
    test_whole_value_equal(void) {
    return (struct N){.a = 1};
}

[[cccc::test(return = (struct A){0})]] struct A test_late_array_element(void) {
    struct A a = {0};
    a.v[8]     = 5;
    return a;
}

[[cccc::test(return = (struct Anon){.ax = 1})]] struct Anon
    test_anonymous_member(void) {
    struct Anon a = {0};
    a.ax          = 2;
    return a;
}

[[cccc::test(return = (union U){.i = 5})]] union U test_union_arm(void) {
    union U u;
    u.i = 6;
    return u;
}

[[cccc::test(return = (struct Bits){.a = -2, .b = 9, .c = 7})]] struct Bits
    test_bit_field(void) {
    return (struct Bits){-2, 8, 7};
}

[[cccc::test(return = (struct Wide){.x = 1.5, .y = 3})]] struct Wide
    test_long_double(void) {
    return (struct Wide){2.5L, 3};
}

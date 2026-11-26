// CCCC_FLAGS: --testing
// CCCC_NATIVE_SKIP: negative (error=) tests
// Suite: implicit conversions involving vectors under the default
// --compiler-family=gcc: only compatible vectors convert.

#pragma cccc suite begin "vector_conversions"

typedef float    v2 __attribute__((vector_size(8)));
typedef float    v4 __attribute__((vector_size(16)));
typedef int      i4 __attribute__((vector_size(16)));
typedef unsigned u4 __attribute__((vector_size(16)));
typedef short    s8 __attribute__((vector_size(16)));
typedef float    e3 __attribute__((ext_vector_type(3)));
typedef float    e4 __attribute__((ext_vector_type(4)));
typedef int      ei4 __attribute__((ext_vector_type(4)));

static i4 give_i4(i4 x) {
    return x;
}

static void take_i4(i4 x) {
    (void)x;
}

static void take_v4(v4 x) {
    (void)x;
}

[[cccc::test(error = "incompatible types when initializing")]]
void test_init_size_mismatch(void) {
    v4 q = {1, 2, 3, 4};
    v2 g = q;
    (void)g;
}

[[cccc::test(error = "incompatible types when initializing")]]
void test_init_element_mismatch(void) {
    v4 q = {1, 2, 3, 4};
    i4 k = q;
    (void)k;
}

[[cccc::test(error = "incompatible types when initializing")]]
void test_init_signedness_mismatch(void) {
    i4 a = {1, 2, 3, 4};
    u4 b = a;
    (void)b;
}

[[cccc::test(error = "incompatible types when initializing")]]
void test_init_lane_mismatch(void) {
    i4 a = {1, 2, 3, 4};
    s8 b = a;
    (void)b;
}

[[cccc::test(error = "incompatible types when assigning")]]
void test_assign_mismatch(void) {
    v4 q = {1, 2, 3, 4};
    i4 k = {0, 0, 0, 0};
    k    = q;
}

[[cccc::test(error = "incompatible types when passing an argument")]]
void test_argument_mismatch(void) {
    v4 q = {1, 2, 3, 4};
    take_i4(q);
}

[[cccc::test(error = "incompatible types when initializing")]]
void test_vector_to_scalar(void) {
    v4    q = {1, 2, 3, 4};
    float f = q;
    (void)f;
}

[[cccc::test(error = "incompatible types when passing an argument")]]
void test_scalar_argument(void) {
    take_v4(2.0f);
}

[[cccc::test(error = "incompatible types when initializing")]]
void test_ext_element_mismatch(void) {
    e4  a = {1, 2, 3, 4};
    ei4 b = a;
    (void)b;
}

[[cccc::test(error = "vector types do not match")]]
void test_ext_visible_lanes_mismatch(void) {
    e3 a = {1, 2, 3};
    e4 b = {1, 2, 3, 4};
    e4 c = a + b;
    (void)c;
}

[[cccc::test(return = 42)]]
int test_identical_types_convert(void) {
    i4 a = {1, 2, 3, 4};
    i4 b = a;
    i4 c = give_i4(b);
    b    = c;
    return b[3] == 4 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_explicit_cast_between_vectors(void) {
    i4 a = {1, 2, 3, 4};
    u4 b = (u4)a;
    return b[2] == 3 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_const_qualified_vector(void) {
    const i4 a = {1, 2, 3, 4};
    i4       b = a;
    return b[0] == 1 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_scalar_broadcast_in_arithmetic(void) {
    v4 a = {1, 2, 3, 4};
    v4 b = a + 1.0f;
    return b[0] == 2 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_comparison_mask_converts(void) {
    u4 a = {1, 2, 3, 4};
    u4 b = {1, 1, 1, 1};
    u4 m = (a <= b);
    i4 n = (a <= b);
    return m[0] == 0xFFFFFFFFu && n[1] == 0 ? 42 : 1;
}

#pragma cccc suite end

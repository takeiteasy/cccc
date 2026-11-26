// CCCC_FLAGS: --testing --compiler-family=clang
// CCCC_NATIVE_SKIP: negative (error=) tests
// Suite: implicit vector conversions under --compiler-family=clang: any
// same-size pair converts (a bit cast) unless both are ext vectors.

#pragma cccc suite begin "vector_conversions_clang"

typedef float v2 __attribute__((vector_size(8)));
typedef float v4 __attribute__((vector_size(16)));
typedef int   i4 __attribute__((vector_size(16)));
typedef short s8 __attribute__((vector_size(16)));
typedef float e4 __attribute__((ext_vector_type(4)));
typedef int   ei4 __attribute__((ext_vector_type(4)));

[[cccc::test(return = 42)]]
int test_same_size_bit_cast(void) {
    v4 q = {1.0f, 2.0f, 3.0f, 4.0f};
    i4 k = q;
    return k[0] == 0x3f800000 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_lane_count_change(void) {
    i4 a = {1, 0, 0, 0};
    s8 b = a;
    return b[0] == 1 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_ext_and_vector_size(void) {
    e4 a = {1, 2, 3, 4};
    v4 b = a;
    i4 c = a;
    return b[1] == 2 && c[0] == 0x3f800000 ? 42 : 1;
}

[[cccc::test(error = "incompatible types when initializing")]]
void test_size_mismatch(void) {
    v4 q = {1, 2, 3, 4};
    v2 g = q;
    (void)g;
}

[[cccc::test(error = "incompatible types when initializing")]]
void test_ext_element_mismatch(void) {
    e4  a = {1, 2, 3, 4};
    ei4 b = a;
    (void)b;
}

[[cccc::test(error = "incompatible types when initializing")]]
void test_vector_to_scalar(void) {
    v4    q = {1, 2, 3, 4};
    float f = q;
    (void)f;
}

#pragma cccc suite end

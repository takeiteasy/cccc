// CCCC_FLAGS: --testing
// CCCC_NATIVE_SKIP: negative (error=) tests
// Suite: malformed ext_vector_type, neon_vector_type and neon_polyvector_type
// uses, and component selectors that clang rejects.

#pragma cccc suite begin "ext_vector_errors"

[[cccc::test(error = "Neon vector size must be 64 or 128 bits")]]
void test_neon_vector_wrong_width(void) {
    typedef int n3 __attribute__((neon_vector_type(3)));
    n3 v;
    (void)v;
}

[[cccc::test(error = "element must be an unsigned")]]
void test_neon_poly_float_element(void) {
    typedef float p __attribute__((neon_polyvector_type(4)));
    p v;
    (void)v;
}

[[cccc::test(error = "element must be an unsigned")]]
void test_neon_poly_signed_element(void) {
    typedef char p __attribute__((neon_polyvector_type(8)));
    p v;
    (void)v;
}

[[cccc::test(error = "not supported: the size must be a power of two no "
                     "larger than 64 bytes")]]
void test_ext_vector_too_wide(void) {
    typedef double d __attribute__((ext_vector_type(16)));
    d v;
    (void)v;
}

[[cccc::test(error = "boolean 'ext_vector_type'")]]
void test_ext_vector_bool_element(void) {
    typedef _Bool b __attribute__((ext_vector_type(4)));
    b v;
    (void)v;
}

[[cccc::test(error = "needs a positive number of lanes")]]
void test_ext_vector_zero_lanes(void) {
    typedef int z __attribute__((ext_vector_type(0)));
    z v;
    (void)v;
}

[[cccc::test(error = "applies only to")]]
void test_ext_vector_struct_element(void) {
    typedef struct {
        int a;
    } S;
    typedef S s4 __attribute__((ext_vector_type(4)));
    s4 v;
    (void)v;
}

[[cccc::test(error = "is not a vector component")]]
void test_mixed_component_sets(void) {
    typedef float f4 __attribute__((ext_vector_type(4)));
    f4 a = {1, 2, 3, 4};
    (void)a.xg;
}

[[cccc::test(error = "names a lane the vector does not have")]]
void test_component_out_of_range(void) {
    typedef float f2 __attribute__((ext_vector_type(2)));
    f2 a = {1, 2};
    (void)a.z;
}

[[cccc::test(error = "names a lane the vector does not have")]]
void test_three_lane_vector_has_no_w(void) {
    typedef float f3 __attribute__((ext_vector_type(3)));
    f3 a = {1, 2, 3};
    (void)a.w;
}

[[cccc::test(error = "not a struct nor a union")]]
void test_vector_size_vector_has_no_components(void) {
    typedef float v4 __attribute__((vector_size(16)));
    v4 a = {1, 2, 3, 4};
    (void)a.x;
}

#pragma cccc suite end

// CCCC_FLAGS: --testing --compiler-family=clang
// Suite: same-shape integer vectors that differ only in signedness under
// --compiler-family=clang: a comparison runs in the left operand's type and
// `?:` accepts the pair.

#pragma cccc suite begin "vector_signedness_clang"

typedef int      i4 __attribute__((vector_size(16)));
typedef unsigned u4 __attribute__((vector_size(16)));
typedef int      ei4 __attribute__((ext_vector_type(4)));
typedef unsigned eu4 __attribute__((ext_vector_type(4)));

[[cccc::test(return = 42)]]
int test_comparison_uses_left_type(void) {
    i4 a   = {-1, 1, 5, 0};
    u4 b   = {1, 1, 1, 1};
    i4 lt  = a < b;
    i4 gt  = a > b;
    i4 lt2 = b < a;
    return lt[0] == -1 && gt[0] == 0 && gt[2] == -1 && lt2[0] == -1 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_conditional_takes_then_type(void) {
    i4 a = {1, 2, 3, 4};
    u4 b = {5, 6, 7, 8};
    i4 c = 1 ? a : b;
    i4 d = 0 ? a : b;
    return __builtin_types_compatible_p(__typeof__(1 ? a : b), i4) &&
                   c[0] == 1 && d[3] == 8
               ? 42
               : 1;
}

[[cccc::test(return = 42)]]
int test_ext_vectors(void) {
    ei4 a = {1, 2, 3, 4};
    eu4 b = {1, 1, 1, 1};
    ei4 s = a + b;
    ei4 m = a == b;
    return s[3] == 5 && m[0] == -1 && m[1] == 0 ? 42 : 1;
}

#pragma cccc suite end

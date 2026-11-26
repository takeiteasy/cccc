// CCCC_FLAGS: --testing
// Suite: same-shape integer vectors that differ only in signedness in
// operators under the default --compiler-family=gcc.

#pragma cccc suite begin "vector_signedness"

typedef int      i4 __attribute__((vector_size(16)));
typedef unsigned u4 __attribute__((vector_size(16)));

[[cccc::test(return = 42)]]
int test_result_takes_left_type(void) {
    i4 a = {1, 2, 3, 4};
    u4 b = {1, 1, 1, 1};
    return __builtin_types_compatible_p(__typeof__(a + b), i4) &&
                   __builtin_types_compatible_p(__typeof__(b + a), u4)
               ? 42
               : 1;
}

[[cccc::test(return = 42)]]
int test_arithmetic(void) {
    i4 a = {-8, 6, 5, 4};
    u4 b = {2, 3, 1, 1};
    i4 d = a / b;
    i4 s = a - b;
    i4 m = a % b;
    return d[0] == -4 && d[1] == 2 && s[0] == -10 && m[1] == 0 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_bitwise(void) {
    i4 a = {12, 10, 0, -1};
    u4 b = {10, 12, 5, 1};
    i4 x = a & b;
    i4 o = a | b;
    i4 e = a ^ b;
    return x[0] == 8 && o[1] == 14 && e[2] == 5 && x[3] == 1 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_compound_assignment(void) {
    i4 a  = {1, 2, 3, 4};
    u4 b  = {10, 10, 10, 10};
    a    += b;
    b    -= a;
    return a[0] == 11 && a[3] == 14 && (int)b[0] == -1 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_comparison_is_unsigned_in_gcc(void) {
    i4 a  = {-1, 1, 5, 0};
    u4 b  = {1, 1, 1, 1};
    i4 lt = a < b;
    i4 gt = a > b;
    i4 eq = a == b;
    return lt[0] == 0 && gt[0] == -1 && eq[1] == -1 && gt[2] == -1 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_comparison_swapped_operands(void) {
    u4 b  = {1, 1, 1, 1};
    i4 a  = {-1, 1, 5, 0};
    i4 lt = b < a;
    i4 le = b <= a;
    return lt[0] == -1 && lt[1] == 0 && le[1] == -1 && le[3] == 0 ? 42 : 1;
}

#pragma cccc suite end

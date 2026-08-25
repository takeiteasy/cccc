// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: requested alignment is less than minimum alignment
//
// #1163: an _Alignas(N) that requests LESS than a struct member's own
// natural alignment is a C17 6.7.5p4 constraint violation -- gcc-16 and
// clang both reject it ("'_Alignas' specifiers cannot reduce alignment of
// 'b'"). cccc previously accepted it silently, laying `b` out
// under-aligned relative to its own type.

struct AlignasLowersMember1163 {
    char a;
    _Alignas(1) long b;
};

int main(void) {
    return 0;
}

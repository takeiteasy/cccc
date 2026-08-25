// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: alignment specified for bit-field
//
// #1165: _Alignas(N)/alignas(N) is rejected outright on a bit-field --
// verified against gcc-16 ("alignment specified for bit-field 'b'"), clang
// agrees. Only the GNU __attribute__((aligned(N))) suffix spelling is
// legal on a bit-field (see test_suite_attributes_layout_1129.c's
// test_bitfield_suffix_aligned_1165), so cccc must reject this specific
// spelling while still parsing and honoring the other.

struct AlignasBitfield1165 {
    char a;
    _Alignas(16) int b : 5;
    int c;
};

int main(void) {
    return 0;
}

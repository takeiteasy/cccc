// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: _Static_assert\(sizeof\(struct lg1172_plain\) == 16,
// CCCC_EXPECT_STDOUT: _Static_assert\(_Alignof\(struct lg1172_plain\) == 8,
// CCCC_EXPECT_STDOUT: _Static_assert\(__builtin_offsetof\(struct lg1172_plain, a\) == 0,
// CCCC_EXPECT_STDOUT: _Static_assert\(__builtin_offsetof\(struct lg1172_plain, b\) == 4,
// CCCC_EXPECT_STDOUT: _Static_assert\(__builtin_offsetof\(struct lg1172_plain, c\) == 8,
// CCCC_HOST_CC
// -m output carries a layout guard for each size, alignment and member offset,
// and the host compiler accepts it.

struct lg1172_plain {
    int  a;
    char b;
    long c;
};
struct lg1172_plain g_plain;

int main(void) {
    return 42;
}

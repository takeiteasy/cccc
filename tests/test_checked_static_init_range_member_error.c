// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --checked-pointers
// CCCC_EXPECT_STDERR: checked initializer.s bounds end 16 bytes past the source array

struct R {
    int *lo;
    int *hi;
    int *[[cccc::array, cccc::bounds(lo, hi)]] p;
};

int      buf[4];
struct R r = {buf, buf + 8, buf};

int main(void) {
    return 42;
}

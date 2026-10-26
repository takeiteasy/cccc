// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --checked-pointers
// CCCC_EXPECT_STDERR: checked initializer needs 16 bytes but the source array has 8

struct T {
    int *[[cccc::array, cccc::count(n)]] p;
    int n;
};

int       buf[2];
struct T *g = &(struct T){buf, 4};

int main(void) {
    return 42;
}

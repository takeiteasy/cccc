// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --checked-pointers
// CCCC_EXPECT_STDERR: checked initializer needs 32 bytes but the source array has 8

struct S {
    int n;
    int *[[cccc::array, cccc::count(n)]] p;
};

int buf[2];

int main(void) {
    static struct S s = {8, buf};
    return s.n;
}

// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --checked-pointers
// CCCC_EXPECT_STDERR: checked initializer needs 12 bytes but the source array has 8

int buf[2];
int *[[cccc::array, cccc::count(3)]] p = buf;

int main(void) {
    return 42;
}

// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --checked-pointers
// CCCC_EXPECT_STDERR: checked initializer.s bounds start 4 bytes before the source array

int buf[4];
int *[[cccc::array, cccc::bounds(buf - 1, buf + 2)]] p = buf;

int main(void) {
    return 42;
}

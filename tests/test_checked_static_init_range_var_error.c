// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --checked-pointers
// CCCC_EXPECT_STDERR: checked initializer.s bounds end 20 bytes past the source array

int buf[4];
int *[[cccc::array, cccc::bounds(buf, buf + 9)]] p = buf;

int main(void) {
    return 42;
}

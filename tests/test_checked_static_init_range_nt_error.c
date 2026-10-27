// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --checked-pointers
// CCCC_EXPECT_STDERR: checked initializer.s bounds end 4 bytes past the source array

int buf[4];
int *[[cccc::ntarray, cccc::bounds(buf, buf + 4)]] p = buf;

int main(void) {
    return 42;
}

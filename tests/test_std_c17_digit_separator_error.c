// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --std=c17
// CCCC_EXPECT_STDERR: digit separators are not available before C23
int main(void) {
    return 1'000'000 > 0 ? 0 : 1;
}

// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: backtick quasi-quotes are only valid in comptime functions

int main(void) {
    return `42`;
}

// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: unclosed #pragma CHECKED_SCOPE push
// A push with no pop is an error at the end of the file.

#pragma CHECKED_SCOPE push

int main(void) {
    return 42;
}

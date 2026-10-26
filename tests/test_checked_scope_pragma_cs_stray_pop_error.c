// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: stray #pragma CHECKED_SCOPE pop
// A pop with nothing pushed is an error.

#pragma CHECKED_SCOPE pop

int main(void) {
    return 42;
}

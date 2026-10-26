// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: CHECKED_SCOPE pop does not match
// pop cannot close a #pragma cccc checked begin.

#pragma cccc checked begin
#pragma CHECKED_SCOPE pop
#pragma cccc checked end

int main(void) {
    return 42;
}

// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: expected 'on', 'off', 'push' or 'pop'
// An unknown operand is an error.

#pragma CHECKED_SCOPE sideways

int main(void) {
    return 42;
}

// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: a '_Checked' region must be followed by a compound statement

int main(void) {
    _Checked return 42;
}

// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: a '_Unchecked' region must be followed by a compound statement

int main(void) {
    int        x = 1;
    _Unchecked x = 2;
    return 42;
}

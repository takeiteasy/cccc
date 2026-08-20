// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -Wcpp -Werror=cpp
// CCCC_EXPECT_STDERR: error: #warning directive \[-Wcpp\]
#warning
int main(void) {
    return 42;
}

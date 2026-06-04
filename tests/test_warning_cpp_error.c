// EXPECT_COMPILE_ERROR
// JCC_FLAGS: -Wcpp -Werror=cpp
// JCC_EXPECT_STDERR: error: #warning directive \[-Wcpp\]
#warning
int main(void) { return 42; }

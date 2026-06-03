// JCC_FLAGS: -Wcpp -Werror -Wno-error=cpp
// JCC_EXPECT_STDERR: warning: warning \[-Wcpp\]
#warning
int main(void) { return 42; }

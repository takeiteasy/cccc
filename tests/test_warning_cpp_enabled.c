// CCCC_FLAGS: -Wcpp
// CCCC_EXPECT_STDERR: warning: #warning directive \[-Wcpp\]
#warning
int main(void) {
    return 42;
}

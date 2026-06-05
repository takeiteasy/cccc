// JCC_FLAGS: -Wunused
// JCC_REJECT_STDERR: unused variable 'x'
// JCC_EXPECT_STDERR: unused variable 'y'
int main(void) {
    _Pragma("GCC diagnostic push")
    _Pragma("GCC diagnostic ignored \"-Wunused\"")
    int x = 1;
    _Pragma("GCC diagnostic pop")
    int y = 2;
    return 42;
}

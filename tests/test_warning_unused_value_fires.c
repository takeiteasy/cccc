// CCCC_FLAGS: -Wunused-value
// CCCC_EXPECT_STDERR: expression result unused.*\[-Wunused-value\]

int main(void) {
    int x = 1, y = 2;
    x + y;
    return 42;
}

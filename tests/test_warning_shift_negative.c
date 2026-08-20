// CCCC_FLAGS: -Wshift-negative-value
// CCCC_EXPECT_STDERR: left shift by negative amount -1 is undefined
// behaviour.*\[-Wshift-negative-value\]

int main(void) {
    int x = 1 << -1;
    (void)x;
    return 42;
}

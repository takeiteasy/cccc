// CCCC_FLAGS: -Wshift-overflow
// CCCC_EXPECT_STDERR: left shift amount 64 >= width of type \(64 bits\).*\[-Wshift-overflow\]

int main(void) {
    long long x = 1LL << 64;
    (void)x;
    return 42;
}

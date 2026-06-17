// CCCC_FLAGS: -Wno-shift-overflow
// CCCC_REJECT_STDERR: shift-overflow

int main(void) {
    long long x = 1LL << 64;
    (void)x;
    return 42;
}

// CCCC_FLAGS: -Wno-shift-negative-value
// CCCC_REJECT_STDERR: shift-negative-value

int main(void) {
    int x = 1 << -1;
    (void)x;
    return 42;
}

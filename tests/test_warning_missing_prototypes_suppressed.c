// CCCC_FLAGS: -Wmissing-prototypes -Wno-missing-prototypes
// CCCC_REJECT_STDERR: warning:

int helper(int x) {
    return x + 1;
}

int main(void) {
    return helper(41);
}

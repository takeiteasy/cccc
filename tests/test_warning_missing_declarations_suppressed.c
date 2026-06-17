// CCCC_FLAGS: -Wmissing-declarations -Wno-missing-declarations
// CCCC_REJECT_STDERR: warning:

int helper(int x) {
    return x + 1;
}

int main(void) {
    return helper(41);
}

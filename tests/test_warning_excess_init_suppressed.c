// CCCC_FLAGS: -Wall -Wno-excess-init
// CCCC_REJECT_STDERR: warning:

int main(void) {
    int a[1] = {1, 2, 3};
    return a[0] + 41;
}

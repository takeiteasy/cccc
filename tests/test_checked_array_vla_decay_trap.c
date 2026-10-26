// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// Decay keeps the VLA's runtime bounds.

int main(void) {
    volatile int n = 5;
    int a _Checked[n];
    int *q = a;
    q[n]   = 1;
    return 42;
}

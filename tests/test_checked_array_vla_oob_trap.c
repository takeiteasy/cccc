// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// A checked VLA is checked against its runtime extent.

int main(void) {
    volatile int n = 5;
    int a _Checked[n];
    a[n] = 1;
    return 42;
}

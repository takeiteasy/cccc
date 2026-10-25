// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: has no bounds in a checked region
// Checked regions: array kind with no bounds form.

extern void *malloc(unsigned long);
int *g_raw;
int  g_x;

[[cccc::checked]]
int f(void) {
    int arr[3]             = {1, 2, 3};
    int *[[cccc::array]] a = arr;
    return a[0];
}

int main(void) {
    return 42;
}

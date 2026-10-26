// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// A checked VLA parameter is `count(n)` on the decayed pointer.

void f(int n, int a _Checked[n]) {
    volatile int i = n;
    a[i]           = 1;
}

int arr[8];

int main(void) {
    f(5, arr);
    return 42;
}

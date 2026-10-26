// EXPECT_COMPILE_ERROR CCCC_FLAGS: --checked-pointers
// A VLA parameter's extent is its pointer's count(n), re-evaluated at every
// access, so it must be side-effect free.

void f(int n, int a _Checked[n++]) {
    a[0] = 1;
}

int main(void) {
    int arr[4];
    f(4, arr);
    return 42;
}

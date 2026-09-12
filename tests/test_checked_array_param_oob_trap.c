// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// #487: `void f(int a _Checked[N])` adjusts to a checked pointer
// (`int * [[cccc::array, cccc::count(N)]] a`) at the same array-to-pointer
// parameter adjustment site an ordinary array parameter goes through --
// closing the asymmetry where a checked region already rejected a plain
// array parameter but had no checked spelling to accept instead.

void f(int a _Checked[5]) {
    volatile int i = 5;
    a[i]           = 1;
}

int arr[6];

int main(void) {
    f(arr);
    return 42;
}

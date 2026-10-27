// CCCC_FLAGS: --checked-pointers

// An OPT local on a path that never rooted it holds the sentinel: no check.
static int take(int *[[cccc::array, cccc::count(n)]] p, int n) {
    return p[2];
}
int main(void) {
    int arr[10]                              = {1, 2, 42};
    int *[[cccc::array, cccc::count(3)]] src = arr;
    int *raw                                 = arr;
    int  c                                   = 0;
    int *q                                   = raw;
    if (c)
        q = src;
    return take(q, 10);
}

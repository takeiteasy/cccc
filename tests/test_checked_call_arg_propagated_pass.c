// CCCC_FLAGS: --checked-pointers

static int take(int *[[cccc::array, cccc::count(n)]] p, int n) {
    return p[n - 1];
}
int main(void) {
    int arr[3]                               = {1, 2, 40};
    int *[[cccc::array, cccc::count(3)]] src = arr;
    int *q                                   = src;
    return take(q, 3) + 2;
}

// CCCC_FLAGS: --checked-pointers

// An in-bounds dynamic cast passed to a checked parameter.
static int take(int *[[cccc::array, cccc::count(n)]] p, int n) {
    return p[n - 1];
}
int main(void) {
    int arr[3]                               = {1, 2, 40};
    int *[[cccc::array, cccc::count(3)]] src = arr;
    int n                                    = 3;
    return take((int *[[cccc::array, cccc::count(n), cccc::dynamic]])src, n) +
           2;
}

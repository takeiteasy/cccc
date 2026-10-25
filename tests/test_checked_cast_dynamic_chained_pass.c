// CCCC_FLAGS: --checked-pointers
// A source chained through two propagated locals.

int main(void) {
    int arr[3]                               = {1, 2, 40};
    int *[[cccc::array, cccc::count(3)]] src = arr;
    int *q                                   = src;
    int *r                                   = q + 1;
    int  n                                   = 2;
    int *[[cccc::array, cccc::count(n)]] p =
        (int *[[cccc::array, cccc::count(n), cccc::dynamic]])r;
    return p[1] + 2;
}

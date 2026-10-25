// CCCC_FLAGS: --checked-pointers
// A dynamic cast from a propagated local is verified against its bounds.

int main(void) {
    int arr[3]                               = {1, 2, 40};
    int *[[cccc::array, cccc::count(3)]] src = arr;
    int *q                                   = src;
    int  n                                   = 3;
    int *[[cccc::array, cccc::count(n)]] p =
        (int *[[cccc::array, cccc::count(n), cccc::dynamic]])q;
    return p[2] + 2;
}

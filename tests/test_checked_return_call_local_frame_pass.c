// CCCC_FLAGS: --checked-pointers -3

// A frame address passed through a hoisted argument is still caught.
static int *[[cccc::array, cccc::bounds(lo, hi)]] window(int *lo, int *hi) {
    return (int *[[cccc::array, cccc::bounds(lo, hi), cccc::assume]])lo;
}
int main(void) {
    int  arr[4] = {1, 2, 3, 4};
    int *q      = window(&arr[0], &arr[4]);
    return q[3] + 38;
}

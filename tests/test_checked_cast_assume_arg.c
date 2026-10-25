// CCCC_FLAGS: --checked-pointers

// An assume cast as an argument takes the claim on trust.
static int take(int *p) {
    return p[0];
}
int main(void) {
    int  arr[3] = {1, 2, 40};
    int *raw    = arr;
    int  n      = 10;
    return take((int *[[cccc::array, cccc::count(n), cccc::assume]])raw) + 41;
}

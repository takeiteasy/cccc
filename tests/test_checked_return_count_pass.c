// CCCC_FLAGS: --checked-pointers

// A returned value is checked against the return type's bounds.
static int *[[cccc::array,
              cccc::count(n)]] pick(int *[[cccc::array, cccc::count(3)]] src,
                                    int n) {
    return src;
}
static int *[[cccc::array, cccc::count(n)]] pick_via_local(
    int *[[cccc::array, cccc::count(3)]] src, int n) {
    int *q = src;
    return q;
}
int main(void) {
    int  arr[3] = {1, 2, 40};
    int *p      = pick(arr, 3);
    int *r      = pick_via_local(arr, 2);
    return p[2] + r[0] + 1;
}

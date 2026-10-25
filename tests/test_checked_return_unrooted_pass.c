// CCCC_FLAGS: --checked-pointers

// An unrooted value has no bounds to verify, so the return isn't checked.
static int *[[cccc::array, cccc::count(n)]] pick(int *raw, int n) {
    return raw;
}
int main(void) {
    int  arr[3] = {1, 2, 40};
    int *p      = pick(arr, 10);
    return p[2] + 2;
}

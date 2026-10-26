// CCCC_FLAGS: --checked-pointers

// An OPT source can hold the no-bounds sentinel, so the assignment is skipped.
int main(void) {
    int arr[10]                              = {1, 2, 40};
    int *[[cccc::array, cccc::count(3)]] src = arr;
    int *raw                                 = arr;
    int  c                                   = 1;
    int *q                                   = raw;
    if (c)
        q = src;
    int n                                  = 10;
    int *[[cccc::array, cccc::count(n)]] p = arr;
    p                                      = q;
    return p[2] + 2;
}

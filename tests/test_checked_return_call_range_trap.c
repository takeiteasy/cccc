// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers

// bounds(lo, hi) naming two parameters.
static int buf[8];
static int *[[cccc::array, cccc::bounds(lo, hi)]] window(int *lo, int *hi) {
    return (int *[[cccc::array, cccc::bounds(lo, hi), cccc::assume]])lo;
}
int main(void) {
    int *q = window(buf + 1, buf + 3);
    return q[0] + q[1] + 42 + q[2];
}

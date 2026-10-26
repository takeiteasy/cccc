// CCCC_FLAGS: --checked-pointers

// A checked-return call inside the callee's own body.
static int buf[8];
static int *[[cccc::array, cccc::count(n)]] head(int n, int depth) {
    if (depth > 0)
        return head(n, depth - 1);
    return (int *[[cccc::array, cccc::count(n), cccc::assume]])buf;
}
int main(void) {
    int *q = head(6, 3);
    q[5]   = 42;
    return q[5];
}

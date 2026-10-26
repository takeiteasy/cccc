// CCCC_FLAGS: --checked-pointers

static int buf[4] = {1, 2, 3, 40};
static int *[[cccc::array, cccc::count(n)]] head(int n) {
    return (int *[[cccc::array, cccc::count(n), cccc::assume]])buf;
}
int main(void) {
    int *q = head(4);
    int *r = q + 1;
    return r[2] + q[1];
}

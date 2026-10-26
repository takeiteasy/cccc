// CCCC_FLAGS: --checked-pointers

static int buf[4] = {1, 2, 3, 38};
static int *[[cccc::array, cccc::count(n)]] head(int n) {
    return (int *[[cccc::array, cccc::count(n), cccc::assume]])buf;
}
int main(void) {
    return head(4)[3] + head(2)[1] + 2;
}

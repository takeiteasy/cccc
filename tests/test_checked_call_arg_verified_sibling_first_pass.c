// CCCC_FLAGS: --checked-pointers
static int k = 2;
static int sink(int *[[cccc::single]] p,
                int *[[cccc::array, cccc::count(*p)]] q) {
    return q[0] + q[1];
}

int main(void) {
    int *[[cccc::array, cccc::count(2)]] small = (int[2]){40, 2};
    int *[[cccc::single]] pk                   = &k;
    return sink(pk, small);
}

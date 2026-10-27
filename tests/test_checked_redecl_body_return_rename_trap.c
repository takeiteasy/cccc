// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// The prototype's return bound `count(n)` is enforced on the renamed
// definition's `return`: `q + 1` leaves one element, not two.

int *[[cccc::array,
       cccc::count(n)]] tail(int *[[cccc::array, cccc::count(n)]] p, int n);

int *tail(int *q, int m) {
    (void)m;
    return q + 1;
}

int main(void) {
    int a[2] = {1, 2};
    return tail(a, 2)[0];
}

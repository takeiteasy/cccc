// CCCC_FLAGS: --checked-pointers
// #486: same over-claim as test_checked_cast_dynamic_trap.c (count(10) from
// a count(3) source), but via [[cccc::assume]] -- takes the claim on trust,
// no runtime check of it. Must run clean (only an in-bounds access is
// performed, so CHKR on the claimed bounds doesn't fire either).

int main(void) {
    int arr[3]                               = {1, 2, 3};
    int *[[cccc::array, cccc::count(3)]] src = arr;
    int n                                    = 10;
    int *[[cccc::array, cccc::count(n)]] p =
        (int *[[cccc::array, cccc::count(n), cccc::assume]])src;
    return p[0] + 41;
}

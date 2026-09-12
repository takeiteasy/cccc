// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// #486: [[cccc::dynamic]] cast -- the claimed bounds (count(10)) are wider
// than the source's own declared bounds (count(3)), so the desugar's
// `__cv = (T [[..., dynamic]])src` assignment traps via #944's CHKAB: the
// source's [lo, lo+12) does not imply the claimed [lo, lo+40).

int main(void) {
    int arr[3]                               = {1, 2, 3};
    int *[[cccc::array, cccc::count(3)]] src = arr;
    int n                                    = 10;
    int *[[cccc::array, cccc::count(n)]] p =
        (int *[[cccc::array, cccc::count(n), cccc::dynamic]])src;
    return p[0];
}

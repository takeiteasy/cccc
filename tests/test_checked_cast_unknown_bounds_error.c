// EXPECT_COMPILE_ERROR
// #486: [[cccc::dynamic]] cannot be combined with bounds(unknown) -- there
// is nothing to verify the claim against. Caught in pointers()'s post-check
// (src/parse_types.c), always on regardless of --checked-pointers.

int main(void) {
    int *raw = 0;
    int *[[cccc::array, cccc::bounds(unknown)]] p =
        (int *[[cccc::array, cccc::bounds(unknown), cccc::dynamic]])raw;
    return p[0];
}

// EXPECT_COMPILE_ERROR
// #486: [[cccc::dynamic]] needs a declared-checked source to verify the
// claim against; a bare unchecked `int *` has none. Always a compile error,
// independent of --checked-pointers (deliberately not passed here) -- see
// check_checked_cast_dynamic_source()'s comment in src/parse_checked.c.

int main(void) {
    int *raw = 0;
    int  n   = 5;
    int *[[cccc::array, cccc::count(n)]] p =
        (int *[[cccc::array, cccc::count(n), cccc::dynamic]])raw;
    return p[0];
}

// CCCC_FLAGS: --checked-pointers
// #486: [[cccc::assume]]/[[cccc::dynamic]] ARE the sanctioned way to
// convert an UNCHECKED pointer into a checked one inside a checked region
// -- exempt from the #485 cast ban ("cast to an unchecked pointer type is
// not allowed in a checked region") rather than needing an
// [[cccc::unchecked]] { ... } escape too. `g_raw`'s declared type is plain
// `int *` (CHECKED_NONE), declared outside any region; without the
// carve-out in cast() (src/parse_expr.c) casting it to a checked type
// inside f's checked body would hit that ban. This is the documented
// replacement for the launder cast #1332 flags.

int *g_raw;

[[cccc::checked]] int f(int n) {
    int *[[cccc::array, cccc::count(n)]] p =
        (int *[[cccc::array, cccc::count(n), cccc::assume]])g_raw;
    return p[0];
}

int main(void) {
    int arr[5] = {1, 2, 3, 4, 5};
    g_raw      = arr;
    return f(5) + 41;
}

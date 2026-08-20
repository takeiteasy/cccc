// EXPECT_COMPILE_ERROR
// A checked-pointer bounds expression naming a bit-field sibling is
// rejected outright (#921) -- nothing downstream extracts a bit-field's
// value through the ND_MEMBER substitution compute_checked_bounds() builds,
// and adding bit-extraction there for no real benefit isn't worth it. See
// check_member_bounds_template() in src/parse.c.

struct S {
    unsigned n : 4;
    int *[[cccc::array, cccc::count(n)]] p;
};

int main(void) {
    struct S s = {4, (int[4]){1, 2, 3, 4}};
    return s.p[0];
}

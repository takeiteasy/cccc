// EXPECT_COMPILE_ERROR
// A struct/union member's checked-pointer bounds expression may only
// reference sibling members or globals (#921) -- an identifier resolving to
// a LOCAL of the enclosing scope is rejected, because the struct type can
// outlive that local. See the ND_VAR/is_local check in
// check_member_bounds_template() in src/parse.c.

int main(void) {
    int n = 4;
    struct S {
        int *[[cccc::array, cccc::count(n)]] p;
    };
    struct S s = {(int[4]){1, 2, 3, 4}};
    return s.p[0];
}

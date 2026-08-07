// EXPECT_COMPILE_ERROR
// A struct member's bounds expression must be side-effect-free too (#921)
// -- resolve_member_checked_bounds() calls the same resolve_bounds_tokens()
// (and therefore the same node_has_side_effects() check) that Obj-rooted
// bounds use, see tests/test_checked_pointers_side_effect_bounds_error.c.

static int g_i = 0;

struct S {
    int * [[cccc::array, cccc::count(g_i++)]] p;
};

int main(void) {
    struct S s = {(int[3]){1, 2, 3}};
    return s.p[0];
}

// EXPECT_COMPILE_ERROR
// A struct member's bounds expression must be side-effect-free too (#921),
// and that check must see through a ternary's branches too (#949) -- see
// tests/test_checked_pointers_ternary_side_effect_bounds_error.c for the
// Obj-rooted case. g_c and g_i are globals, not locals, so this exercises
// node_has_side_effects() itself rather than
// check_member_bounds_template()'s separate enclosing-local rejection.

static int g_c = 1;
static int g_i = 0;

struct S {
    int * [[cccc::array, cccc::count(g_c ? g_i++ : 3)]] p;
};

int main(void) {
    struct S s = {(int[3]){1, 2, 3}};
    return s.p[0];
}

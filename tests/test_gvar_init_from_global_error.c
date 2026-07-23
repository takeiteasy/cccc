// EXPECT_COMPILE_ERROR
// A global initialized by copying another global's value (as opposed to a
// compound literal, see test_gvar_init_from_compound_literal.c) is not a
// C constant expression, even when the source global is itself
// constant-initialized -- verified against real GCC/clang, both reject this
// with "initializer element is not a compile-time constant". CCCC's
// write_gvar_data gates its #720 splice-in path on Obj.is_compound_literal
// specifically to avoid silently (and incorrectly) accepting this case.

struct P {
    int x;
    int y;
};

struct P g1src = {1, 2};
struct P g2 = g1src; // error: not a compile-time constant

int main(void) {
    return g2.x;
}

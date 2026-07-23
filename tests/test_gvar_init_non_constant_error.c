// EXPECT_COMPILE_ERROR
// A global initialized from a genuinely non-constant expression (a function
// call) must be rejected, not silently zero-filled. Companion to
// test_gvar_init_from_compound_literal.c, which covers the constant cases
// write_gvar_data now knows how to serialize (compound literals and
// global-to-global copies).

struct P {
    int x;
    int y;
};

struct P get(void);

struct P g = get(); // error: not a compile-time constant

int main(void) {
    return g.x;
}

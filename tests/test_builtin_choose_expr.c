// __builtin_choose_expr(const, e1, e2) selects an arm at compile time and
// carries that arm's *type* (unlike "?:", which fuses both arms via the usual
// arithmetic conversions).  The unchosen arm must not be evaluated.

static int side_effects = 0;

static int bump(void) { side_effects++; return 1; }

int main(void) {
    // Type is taken from the chosen arm: choosing an int* arm yields int*,
    // which can then be dereferenced as an lvalue.
    int v = 100;
    int *p = __builtin_choose_expr(1, &v, (void *)0);
    *p = 42;
    if (v != 42) return 1;

    // The unchosen arm is not evaluated (no side effects from bump()).
    int r = __builtin_choose_expr(1, 7, bump());
    if (r != 7 || side_effects != 0) return 2;

    int r2 = __builtin_choose_expr(0, bump(), 9);
    if (r2 != 9 || side_effects != 0) return 3;

    // Selecting between mismatched arm types resolves to the chosen one.
    double d = __builtin_choose_expr(0, 5, 21.0);  // chooses the double arm
    if (d != 21.0) return 4;

    return v;  // 42
}

// Ticket #1042(b): a comma-expression must stay parenthesized wherever
// src/serialize.c re-emits it into a syntax position where a bare comma is
// itself a separator (a function-call argument list, a multi-declarator
// `for`-init clause, or a manually-printed `X = ...;` outside serialize_expr's
// own ND_ASSIGN case) -- get_precedence(ND_COMMA) is the lowest of any node
// kind (src/serialize.c), but the call sites at each of those positions used
// to pass parent_prec 0, so `need_parens` never fired.
//
// Found while auditing test_minilua.c (#1042): a macro like `ivalue(r)`
// expanding to a genuine comma-expression AST node, passed as a single
// call argument, used to serialize as `f(a, (void)0 , b)` -- three
// arguments to a two-parameter function ("too many arguments to function
// call"). Minimized here without needing minilua's own macros.

#define TWO_THEN_INC(x) ((x), (x) + 1)

static int add2(int a, int b) {
    return a + b;
}

int main(void) {
    // A comma-expression funcall argument must not split into extra args.
    int r = add2(TWO_THEN_INC(5), 10); // (5, 6) -> 6, so add2(6, 10) == 16
    if (r != 16)
        return 1;

    // A comma expression inside one declarator's own initializer, combined
    // with the for-loop's own comma-joining of multiple declarators --
    // must not collapse into one flat comma list (which would silently
    // pick the wrong value for i).
    int total = 0;
    for (int i = (1, 2, 3), j = 100; i < 6; i++, j += 100) {
        total += i + j;
    }
    // i runs 3,4,5 (starts at 3, the comma expression's last value);
    // j runs 100,200,300.
    if (total != (3 + 100) + (4 + 200) + (5 + 300))
        return 2;

    return 42;
}

// -c=native, a comma-expression re-emitted into a function-call argument
// position (e.g. a macro like minilua's `ivalue(r)` expanding to one,(b)) used
// to split into extra arguments ('too many arguments to function call') --
// get_precedence(ND_COMMA) is the lowest of any node kind (serialize.c) but the
// call sites at a funcall's argument list, a multi-declarator for-init clause,
// and a handful of manually-printed 'X = ...;' initializers outside
// serialize_expr's own ND_ASSIGN case all passed parent_prec 0, so the wrapping
// parens never fired. Asserts VM 42 -> native 42

#define TWO_THEN_INC(x) ((x), (x) + 1)

static int add2(int a, int b) {
    return a + b;
}

int main(void) {
    int r = add2(TWO_THEN_INC(5), 10);
    if (r != 16)
        return 1;

    int total = 0;
    for (int i = (1, 2, 3), j = 100; i < 6; i++, j += 100) {
        total += i + j;
    }
    if (total != (3 + 100) + (4 + 200) + (5 + 300))
        return 2;

    return 42;
}

// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: goto \*\(
// CCCC_REJECT_STDOUT: unsupported expr kind
//
// [GNU] labels-as-values round-trip: `&&label` is an expression and
// `goto *ptr` is a statement (it is parsed by stmt() and consumes its own
// `;`), so the two halves live in different serializer switches even though
// the audit files both under the expression kinds. gcc and clang both
// support the extension, so the emitted form is the source form.

int main(void) {
    void *tab[2];
    tab[0] = &&one;
    tab[1] = &&two;
    int acc = 0;
    int i = 0;
    goto *tab[i];
one:
    acc += 20;
    goto *tab[1];
two:
    acc += 22;
    return acc;
}

// The backward form of test_macros_quote_label_val_forward_1253.c: `&&label`
// inside a Quote() template where the label is already in scope when the
// address is taken. The ticket claimed this case worked; it did not -- a
// named `goto` inside a template emitted no jump at all, so the "backward"
// probe only appeared to pass by falling through. Both directions are fixed.

[[cccc::comptime]]
Node *g(Node *unused) {
    return Quote("{ int n = 0; void *p;"
                 "  spin: n++; p = &&spin;"
                 "  if (n < 3) goto *p;"
                 "  return n == 3 ? 42 : 1; }");
}

int test(void) {
    g(0);
}

int main(void) {
    return test();
}

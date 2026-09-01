// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: use of undeclared label
//
// A `goto` in a Quote() template whose label is defined neither in the
// template nor in the enclosing function is a hard error. Before, template
// label references were never resolved at all, so this compiled silently and
// the goto was a no-op.

[[cccc::comptime]]
Node *g(Node *unused) {
    return Quote("{ goto nowhere; return 1; }");
}

int test(void) {
    g(0);
    return 42;
}

int main(void) {
    return test();
}

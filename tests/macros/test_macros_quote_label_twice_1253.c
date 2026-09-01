// The same label-defining Quote() template expanded twice in one function.
// Each expansion gets its own ND_LABEL (distinct unique_label) and its goto
// binds within its own copy: if either goto fell through or bound to the
// other copy's label, `r` would stay 99 and the function returns 7.
//
// This also exercises the -c=native/-c=generated serializer path, which must
// give the second expansion's `body:` a fresh spelling (two identical C
// labels in one function are illegal).

[[cccc::comptime]]
Node *chk(Node *unused) {
    return Quote("{ int r = 0; goto body; r = 99;"
                 "  body: r = 42; if (r != 42) return 7; }");
}

int test(void) {
    chk(0);
    chk(0);
    return 42;
}

int main(void) {
    return test();
}

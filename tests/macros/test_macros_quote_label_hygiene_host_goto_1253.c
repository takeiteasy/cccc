// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: use of undeclared label
//
// Template labels are hygienic -- private to the template. The enclosing
// function cannot `goto` into a label defined inside a Quote() template
// spliced into it. (An eager Quote() already rejected this at host-parse
// time; the point of the test is to keep that guarantee under the new
// resolution passes.)

[[cccc::comptime]]
Node *emit_label(Node *unused) {
    return Quote("{ inner: ; }");
}

int test(void) {
    goto inner; // must NOT bind to the template's `inner:`
    emit_label(0);
    return 1;
}

int main(void) {
    return test();
}

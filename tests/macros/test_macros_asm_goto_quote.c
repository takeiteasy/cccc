// An `asm goto` inside a Quote() template names a label the enclosing function
// defines. The template's free label reference is bound after macro expansion,
// the same as a plain `goto` in a template.

[[cccc::comptime]]
Node *skip_to_done(void) {
    return Quote("__asm__ goto(\"\" : : : : done);");
}

[[cccc::comptime]]
Node *private_label(void) {
    return Quote("{ __asm__ goto(\"\" : : : : inner); inner:; }");
}

int test(void) {
    int r = 0;
    skip_to_done();
    private_label();
    r = 42;
done:
    return r;
}

int main(void) {
    return test();
}

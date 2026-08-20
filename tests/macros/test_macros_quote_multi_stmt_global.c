// Ticket #955: an unbraced multi-statement Quote() template returned from a
// file-scope macro call exercises the token-level re-injection in
// src/macros.c (which splices a returned ND_BLOCK's body tokens into the
// stream) against a block synthesized by wrapping the template in braces,
// rather than one the caller wrote braces around directly. Companion of
// tests/macros/test_global_block_expansion.c, which covers the
// already-braced form.

[[cccc::comptime]]
Node *emit_widget2_helpers(void) {
    return Quote("struct Widget2 { int x; int y; }; "
                 "void widget2_init(struct Widget2 *w) { w->x = 0; w->y = 0; } "
                 "void widget2_set(struct Widget2 *w, int x, int y) { w->x = "
                 "x; w->y = y; }");
}

emit_widget2_helpers();

int main(void) {
    struct Widget2 w;
    widget2_init(&w);
    if (w.x != 0 || w.y != 0)
        return 1;
    widget2_set(&w, 3, 7);
    if (w.x != 3 || w.y != 7)
        return 2;
    return 42;
}

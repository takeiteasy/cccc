// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: static const void \*__cccc_tab_0.*=.*&&zero
// CCCC_REJECT_STDOUT: unresolved relocation
//
// Ticket #1044 companion to test_serialize_static_label_table_1044.c: a
// compile-only `-m` check that the deferred static's real definition (with
// its `&&label` initializer intact) is printed inside dispatch()'s own
// body -- not hoisted to file scope, where a label's address has no C
// spelling -- and that today's diagnostic ("cannot serialize initializer
// for global ... unresolved relocation target") never fires. See the
// sibling test for the full VM/native round-trip proof.

static int dispatch(int idx) {
    static const void *tab[] = {&&zero, &&one};
    goto              *tab[idx];
zero:
    return 0;
one:
    return 42;
}

int main(void) {
    return dispatch(1);
}

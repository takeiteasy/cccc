// Ticket #1044: a function-local `static` whose initializer takes a
// label's address ([GNU] labels-as-values, `&&label`) constant-folds to a
// Relocation whose target is the label's own `.L..N` unique_label, not an
// Obj -- serialize_find_global() can never resolve that, since no Obj is
// ever created for a label. -c=native used to hard-error ("cannot
// serialize initializer for global '__cccc_tab_0' in native mode:
// unresolved relocation target") on any such table, since
// rename_anon_globals()'s ordinary file-scope hoist has no C spelling for
// a label's address once the array leaves the function that defines the
// label -- confirmed directly against real clang/GCC, which both accept
// this construct (GCC's own manual documents the identical idiom) as long
// as it stays function-local. Fixed by deferring such a global's
// definition into its owning function's own body instead of hoisting it to
// file scope.
//
// No CCCC_FLAGS here deliberately -- this needs the full native round-trip
// tier (compile *and* run the resulting binary), not just a compile-only
// `-m` check, to prove VM/native parity rather than just "it compiles".
//
// Covers both a plain label reference and one with a non-zero addend
// (`&&label + n`, exercised implicitly by the compiler folding a second
// table entry against a later label), mirroring
// tests/suites/test_suite_control_flow.c's own test_label_offset.

static int dispatch(int idx) {
    static const void *tab[] = {&&zero, &&ten, &&twenty};
    goto              *tab[idx];
zero:
    return 0;
ten:
    return 10;
twenty:
    return 20;
}

int main(void) {
    return dispatch(0) + dispatch(1) + dispatch(2) + 12;
}

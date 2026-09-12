// CCCC_FLAGS: --checked-pointers
// #488: recorded v1 decision -- a prototype and a later definition
// declaring DIFFERENT bounds for the same parameter is accepted silently
// (function()'s redeclaration handling only ever replaces fn->ty when
// fn->is_implicit, src/parse_decl.c), so whichever declaration was seen
// FIRST is the Type every call site's template resolves against. Here
// the prototype (seen first) declares count(n); the definition declares
// count(n + 100) -- if the DEFINITION's bounds won, `sink(small, 2)`
// below would trap (small only has room for 2, and count(102) would
// wrongly demand 102). Asserting a clean exit pins "prototype wins,
// silently" as the actual, tested behaviour, not just a documented
// intent. A prototype/definition bounds-disagreement diagnostic is
// filed as a follow-up.

void sink(int *[[cccc::array, cccc::count(n)]] p, int n);

int main(void) {
    int *[[cccc::array, cccc::count(2)]] small = (int[2]){1, 2};
    sink(small, 2);
    return 42;
}

void sink(int *[[cccc::array, cccc::count(n + 100)]] p, int n) {
    (void)p;
    (void)n;
}

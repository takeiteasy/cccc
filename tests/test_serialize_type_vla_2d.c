// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: int v\[n\]\[m\];
// CCCC_REJECT_STDOUT: int\[m\]
//
// #964: serialize_type_decl()'s TY_VLA branch used to print base-then-name-
// then-`[len]` directly, which mis-spelled a VLA-of-VLA as `int[m] v[n]`
// (an invalid declarator -- the outer dimension's base was printed before
// recursing into it, instead of after). Rewritten to build the declarator
// through the same buffer-recursion shape TY_ARRAY uses, so dimensions
// accumulate in the right order: `int v[n][m]`.
//
// -m only: multi-dimensional VLA *subscript* still SIGSEGVs in the VM (a
// pre-existing parse/codegen bug, unrelated to serialization -- filed
// separately as #971), so there is no round-trip half for this case. This
// test asserts only that the declarator itself serializes correctly; -m
// does not execute the program.

int main(void) {
    int n = 2, m = 3;
    int v[n][m];
    v[1][2] = 42;
    return 42;
}

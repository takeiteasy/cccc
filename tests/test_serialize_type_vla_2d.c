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
// #971 (fixed): multi-dimensional VLA *subscript* used to SIGSEGV in the VM
// (an unrelated codegen bug, not a serialization one -- gen_expr's ND_DEREF
// case loaded through a VLA-typed intermediate row instead of treating it
// as address-based like TY_ARRAY) and the -m output for a pointer-to-VLA row
// mis-spelled `int (*)[m]` as `int *[m]` (serialize_type_decl's TY_PTR
// branch parenthesized only for TY_ARRAY/TY_FUNC bases, not TY_VLA). Both
// are now fixed, so this program round-trips as real, compilable C -- see
// tools/comptime_native_smoke.py's VLA cases for the VM-vs-native assertion.

int main(void) {
    int n = 2, m = 3;
    int v[n][m];
    v[1][2] = 42;
    return 42;
}

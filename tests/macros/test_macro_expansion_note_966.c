// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: undefined variable 'undefined_thing_inner'
// CCCC_EXPECT_STDERR: note: in expansion of macro 'inner' at
// Ticket #966: a compile error inside code a comptime macro generated must
// carry a "note: in expansion of ..." line, not just report the error at
// the generated location with no record of how the compiler got there.
// `outer` expands to a call to `inner`; `inner` generates code that fails
// to compile. The note names `inner`, the macro whose generated token the
// error is actually attached to.

[[cccc::comptime]]
Node *inner(void) {
    return Quote("undefined_thing_inner");
}

[[cccc::comptime]]
Node *outer(void) {
    return Quote("inner()");
}

int main(void) {
    return outer();
}

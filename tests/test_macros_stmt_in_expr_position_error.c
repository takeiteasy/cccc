// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: cannot be used in expression position
//
// Ticket #1248 (root cause corrected -- the ticket's own $@N/brace-wrap
// theory did not hold up, see SCRATCH.md): a comptime function whose
// returned Node* is a statement (ND_BLOCK here, from Quote()'s multi-
// statement brace-auto-wrap) is used in expression position (`int r =
// gen();`). No $@N splice is involved at all -- this is the minimal repro.
// Previously this reached codegen and failed with an opaque "codegen:
// unsupported expression node kind 32"; it must now be a clear diagnostic
// naming the offending comptime function.

[[cccc::comptime]]
Node *gen(void) {
    return Quote("int i; i = 3; return i;");
}

int test(void) {
    int r = gen();
    return r;
}

int main(void) {
    return test();
}

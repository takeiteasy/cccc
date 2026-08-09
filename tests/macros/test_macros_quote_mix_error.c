// EXPECT_COMPILE_ERROR
// Test ticket #1: mixing $N positional and $$ incremental in one template
// should produce a compile-time error.

[[cccc::comptime]]
Node *bad_mix(Node *a, Node *b) {
    // $1 and $$ in the same template — must error
    return __builtin_quote("$1 + $$", a, b);
}

int main(void) {
    int x = bad_mix(1, 2);
    return x;
}

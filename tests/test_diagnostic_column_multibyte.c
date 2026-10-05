// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: in expansion of macro 'boom' at \S+:13:34
// A token's column counts display width, not bytes: é is 1 column, 漢 is 2
// and U+200B is 0. Earlier tokens on the same line must not shift it.

[[cccc::comptime]]
Node *boom(void) {
    return Quote("undefined_thing");
}

int main(void) {
    // clang-format off
    int b = 2; /* é漢​ */ int c = boom();
    // clang-format on
    return 42 + b + c;
}

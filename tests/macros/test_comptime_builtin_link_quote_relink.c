// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: already in another list
// A statement placed into two Quote blocks cannot be linked into both.
int g;

[[cccc::comptime]]
Node *probe(void) {
    Node *a    = Quote("g += 1;");
    Node *x    = Quote("{ $1; g += 10; }", a);
    Node *y    = Quote("{ $1; g += 100; }", a);
    Node *s[2] = {x, y};
    return __builtin_ast_block(s, 2);
}

int main(void) {
    probe();
    return 0;
}

// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: <cccc macro: generated expression>:1: generated expression

[[cccc::comptime(inline)]]
Node *synthetic_loc(void) {
    Node *node = MakeIntLiteral(0);
    SetToken(node, SyntheticToken("generated expression"));
    MacroErrorAt(node, "synthetic generated location");
    return node;
}

int main(void) {
    return synthetic_loc();
}

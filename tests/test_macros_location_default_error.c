// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR:
// test_macros_location_default_error\.c:[0-9]+:.*default_loc

[[cccc::comptime]]
Node *default_loc(void) {
    Node *node = MakeBinary(NK_ADD, MakeIntLiteral(1), MakeIntLiteral(2));
    MacroErrorAt(node, "default generated location");
    return node;
}

int main(void) {
    return default_loc();
}

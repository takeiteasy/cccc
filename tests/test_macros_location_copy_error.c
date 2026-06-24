// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: test_macros_location_copy_error\.c:13:.*copy_loc\(40\)

[[cccc::comptime]]
Node *copy_loc(Node *value) {
    Node *node = MakeBinary(NK_ADD, value, MakeIntLiteral(1));
    CopyLocation(node, value);
    MacroErrorAt(node, "copied generated location");
    return node;
}

int main(void) {
    return copy_loc(40);
}

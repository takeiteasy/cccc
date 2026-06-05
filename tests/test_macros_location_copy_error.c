// EXPECT_COMPILE_ERROR
// JCC_EXPECT_STDERR: test_macros_location_copy_error\.c:13:.*copy_loc\(40\)

[[jcc::macro(inline)]]
_Node *copy_loc(_Node *value) {
    _Node *node = _AST_BINARY(_ADD, value, _AST_INT_LITERAL(1));
    _AST_COPY_LOCATION(node, value);
    _MACRO_ERROR_AT(node, "copied generated location");
    return node;
}

int main(void) {
    return copy_loc(40);
}

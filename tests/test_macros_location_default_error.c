// EXPECT_COMPILE_ERROR
// JCC_EXPECT_STDERR: test_macros_location_default_error\.c:[0-9]+:.*default_loc

[[jcc::macro(inline)]]
_Node *default_loc(void) {
    _Node *node = _AST_BINARY(_ADD, _AST_INT_LITERAL(1), _AST_INT_LITERAL(2));
    _MACRO_ERROR_AT(node, "default generated location");
    return node;
}

int main(void) {
    return default_loc();
}

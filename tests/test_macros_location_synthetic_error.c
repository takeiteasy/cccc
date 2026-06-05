// EXPECT_COMPILE_ERROR
// JCC_EXPECT_STDERR: <jcc macro: generated expression>:1: generated expression

[[jcc::macro(inline)]]
_Node *synthetic_loc(void) {
    _Node *node = _AST_INT_LITERAL(0);
    _AST_SET_TOKEN(node, _AST_SYNTHETIC_TOKEN("generated expression"));
    _MACRO_ERROR_AT(node, "synthetic generated location");
    return node;
}

int main(void) {
    return synthetic_loc();
}

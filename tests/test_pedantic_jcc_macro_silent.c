// JCC_FLAGS: -std=c99 -Wpedantic
// JCC_REJECT_STDERR: \[-Wpedantic\]
[[jcc::macro(inline)]]
_Node *get_42(void) {
    return _AST_INT_LITERAL(42);
}
int main(void) { return get_42(); }

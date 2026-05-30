// EXPECT_COMPILE_ERROR
// Ordinary runtime functions are not lazily compiled into macro bytecode.

int runtime_helper(int n) {
    return n + 1;
}

#pragma macro
_Node *uses_runtime_helper(void) {
    return _AST_INT_LITERAL(runtime_helper(41));
}

int main(void) {
    return uses_runtime_helper();
}

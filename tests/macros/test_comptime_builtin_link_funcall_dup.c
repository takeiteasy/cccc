// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: argument node appears twice in one call
// A builtin that would link a node into a cycle is a compile error.
int callee();

[[cccc::comptime]]
Node *probe(void) {
    Node *arg      = MakeIntLiteral(1);
    Node *args[2] = {arg, arg};
    return __builtin_ast_funcall(__builtin_ast_var_ref("callee"), args, 2);
}

int main(void) {
    probe();
    return 0;
}

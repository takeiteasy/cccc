// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: already in another list
// An uncast call argument is linked in place, so two calls cannot share it.
int callee();

[[cccc::comptime]]
Node *probe(void) {
    Node *arg   = MakeIntLiteral(1);
    Node *one[1] = {arg};
    Node *fn    = __builtin_ast_var_ref("callee");
    __builtin_ast_funcall(fn, one, 1);
    return __builtin_ast_funcall(fn, one, 1);
}

int main(void) {
    probe();
    return 0;
}

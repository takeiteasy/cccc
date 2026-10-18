// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: loop would contain itself
// A builtin that would link a node into a cycle is a compile error.
[[cccc::comptime]]
Node *probe(void) {
    Node *loop = __builtin_ast_while(MakeIntLiteral(1), __builtin_ast_block(0, 0));
    __builtin_ast_loop_set_body(loop, loop);
    return loop;
}

int main(void) {
    probe();
    return 0;
}

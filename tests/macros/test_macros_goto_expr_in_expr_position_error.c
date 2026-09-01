// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: cannot be used in expression position
//
// The mirror of test_macros_goto_expr_stmt_1251.c: a comptime function
// returning a computed goto (ND_GOTO_EXPR) used in expression position must
// be a clear diagnostic naming the offending function, not an opaque
// "codegen: unsupported expression node kind 34". node_is_stmt_kind() now
// recognises ND_GOTO_EXPR, so transform_node's !stmt_pos guard catches it.

[[cccc::comptime]]
Node *jump(Node *target) {
    return Quote("goto *$1;", target);
}

int test(void) {
    void *dst = &&landing;
    int r = jump(dst); // expression position -- not a value
    return r;
landing:
    return 42;
}

int main(void) {
    return test();
}

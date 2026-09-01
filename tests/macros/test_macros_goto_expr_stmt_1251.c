// A comptime function whose Quote()d template is a computed goto
// (`goto *p;`, labels-as-values) and is called in statement position must be
// lifted out of its ND_EXPR_STMT wrapper like any other statement-kind
// return -- node_is_stmt_kind() now classifies ND_GOTO_EXPR as a statement,
// matching gen_stmt() in src/codegen_stmt.c. Before, it reached codegen as a
// bare expression node and failed with "unsupported expression node kind 34".

[[cccc::comptime]]
Node *jump(Node *target) {
    return Quote("goto *$1;", target);
}

int test(void) {
    void *dst = &&landing;
    jump(dst); // statement position -- splices in directly
    return 1;
landing:
    return 42;
}

int main(void) {
    return test();
}

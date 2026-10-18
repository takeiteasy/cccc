// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: switch would contain itself
// A builtin that would link a node into a cycle is a compile error.
[[cccc::comptime]]
Node *probe(void) {
    Node *sw = __builtin_ast_switch(MakeIntLiteral(1));
    __builtin_ast_switch_add_case(sw, MakeIntLiteral(1), sw);
    return sw;
}

int main(void) {
    probe();
    return 0;
}

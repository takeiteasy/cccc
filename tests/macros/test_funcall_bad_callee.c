// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: MakeFuncCall: callee is not a function
// A callee that is not a function is an error, not a NULL node.
int not_a_function;

[[cccc::comptime]]
Node *probe(void) {
    return MakeFuncCall(MakeVarRef("not_a_function"), NULL, 0);
}

int main(void) {
    return probe();
}

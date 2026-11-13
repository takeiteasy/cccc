// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: MakeFuncCall: argument 2 is NULL
// A NULL argument among the first n is an error, not the end of the list.
int callee(int, int);

[[cccc::comptime]]
Node *probe(void) {
    Node *args[2] = {MakeIntLiteral(1), NULL};
    return MakeFuncCall(MakeVarRef("callee"), args, 2);
}

int main(void) {
    return probe();
}

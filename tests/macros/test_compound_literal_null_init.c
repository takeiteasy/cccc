// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: CompoundLiteral: initializer 2 is NULL
// A NULL initializer is an error, not the end of the initializer list.
[[cccc::comptime]]
Node *probe(void) {
    Node *inits[2] = {MakeIntLiteral(1), NULL};
    return CompoundLiteral(GetType("int"), inits[0], inits[1]);
}

int main(void) {
    probe();
    return 0;
}

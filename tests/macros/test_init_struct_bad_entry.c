// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: InitStruct: no such member
// A member name the type lacks is an error, not a NULL node.
typedef struct {
    int x;
} P;

[[cccc::comptime]]
Node *probe(void) {
    const char *flds[1] = {"nope"};
    Node       *vals[1] = {MakeIntLiteral(1)};
    return InitStruct(GetType("P"), flds, vals, 1);
}

int main(void) {
    probe();
    return 0;
}

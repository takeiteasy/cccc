// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: InitStruct: value 1 is NULL
// A NULL value is an error, not a skipped member.
typedef struct {
    int x;
} P;

[[cccc::comptime]]
Node *probe(void) {
    const char *flds[1] = {"x"};
    Node       *vals[1] = {NULL};
    return InitStruct(GetType("P"), flds, vals, 1);
}

int main(void) {
    probe();
    return 0;
}

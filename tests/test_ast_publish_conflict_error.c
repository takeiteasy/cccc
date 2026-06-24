// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: <cccc macro: publish conflict>:1: publish conflict
// CCCC_EXPECT_STDERR: error: conflicting declaration for generated global variable 'conflict_name'

[[cccc::comptime]]
Node *publish_conflicting_global(void) {
    Obj *g = GlobalVar("conflict_name", GetType("int"));
    PublishNodeAt(g, SyntheticToken("publish conflict"));
    return MakeIntLiteral(0);
}

int main(void) {
    int conflict_name = 1;
    return publish_conflicting_global() + conflict_name;
}

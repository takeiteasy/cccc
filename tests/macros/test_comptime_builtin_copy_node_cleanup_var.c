// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: cleanup variable
void release(int *p) { (void)p; }

[[cccc::comptime]]
Node *probe(void) {
    Node *s = Quote("{ __attribute__((cleanup(release))) int x = 1; }");
    return CopyNode(s);
}

int main(void) {
    probe();
    return 0;
}

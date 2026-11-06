// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: macro 'pick' called with 1 arguments; expected 2

@comptime Node *pick(Node *a, Node *b) {
    return a;
}

int main(void) {
    return pick(42);
}

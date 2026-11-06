// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: keyed arguments do not apply to a @comptime function

@comptime Node *pick(Node *a, Node *b) {
    return a;
}

int main(void) {
    return pick(.a = 42, .b = 1);
}

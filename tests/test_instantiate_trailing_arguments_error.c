// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: InstantiateType: unexpected
@template(T) struct Box {
    T value;
};

@comptime Node *bad(void) {
    Type *t = InstantiateType("Box", "int) junk (");
    return MakeIntLiteral(0);
}

int main(void) {
    return bad();
}

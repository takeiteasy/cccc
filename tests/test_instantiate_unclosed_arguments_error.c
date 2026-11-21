// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: InstantiateType: unclosed
@template(T) struct Box {
    T value;
};

@comptime Node *bad(void) {
    Type *t = InstantiateType("Box", "(int");
    return MakeIntLiteral(0);
}

int main(void) {
    return bad();
}

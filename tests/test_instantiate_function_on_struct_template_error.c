// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: InstantiateFunction: 'Box' is a struct template
@template(T) struct Box {
    T value;
};

@comptime Node *bad(void) {
    Obj *f = InstantiateFunction("Box", "int");
    return MakeIntLiteral(0);
}

int main(void) {
    return bad();
}

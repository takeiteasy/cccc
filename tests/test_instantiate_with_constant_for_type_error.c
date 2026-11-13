// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: InstantiateTypeWith: argument 1 of 'Box' is a constant but the parameter is a type
@template(T) struct Box {
    T value;
};

@comptime Node *bad(void) {
    TemplateArgument a[] = {ValueArg(MakeIntLiteral(4))};
    Type            *t   = InstantiateTypeWith("Box", a, 1);
    return MakeIntLiteral(0);
}

int main(void) {
    return bad();
}

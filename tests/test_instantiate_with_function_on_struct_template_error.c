// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: InstantiateFunctionWith: 'Box' is a struct template
@template(T) struct Box {
    T value;
};

@comptime Node *bad(void) {
    TemplateArgument a[] = {TypeArg(GetType("int"))};
    Obj             *f   = InstantiateFunctionWith("Box", a, 1);
    return MakeIntLiteral(0);
}

int main(void) {
    return bad();
}

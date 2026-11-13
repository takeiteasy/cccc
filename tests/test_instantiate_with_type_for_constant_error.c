// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: InstantiateTypeWith: argument 2 of 'Arr' is a type but the parameter is a constant
@template(T, int N) struct Arr {
    T v[N];
};

@comptime Node *bad(void) {
    TemplateArgument a[] = {TypeArg(GetType("int")), TypeArg(GetType("int"))};
    Type            *t   = InstantiateTypeWith("Arr", a, 2);
    return MakeIntLiteral(0);
}

int main(void) {
    return bad();
}

// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: InstantiateType: 'maxof' is a function template
@template(T) T maxof(T a, T b) {
    return a > b ? a : b;
}

@comptime Node *bad(void) {
    Type *t = InstantiateType("maxof", "int");
    return MakeIntLiteral(TypeSize(t));
}

int main(void) {
    return bad();
}

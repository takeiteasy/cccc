// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: 'nothing' is not a template
@comptime Node *bad(void) {
    Type *t = InstantiateType("nothing", "int");
    return MakeIntLiteral(0);
}

int main(void) {
    return bad();
}

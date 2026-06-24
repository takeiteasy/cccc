// EXPECT_COMPILE_ERROR
// Generated functions must not clobber existing definitions.

int existing_function(void) {
    return 1;
}

[[cccc::comptime]]
Node *clobber_existing(void) {
    Type *int_ty = GetType("int");
    Obj *fn = MakeFunction("existing_function", int_ty);
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(42)));
    return MakeIntLiteral(0);
}

int main(void) {
    return clobber_existing();
}

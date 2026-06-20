// Test ticket #122: explicit pragma macro calls at file scope.

[[cccc::comptime]]
Node *generate_answer(void) {
    Type *int_ty = GetType("int");
    Obj *fn = MakeFunction("generated_answer", int_ty);

    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(42)));

    return MakeIntLiteral(0);
}

generate_answer();

int main(void) {
    return generated_answer();
}

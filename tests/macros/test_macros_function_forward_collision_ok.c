// Test that generated functions may still promote existing declarations.

int generated_answer(void);

[[cccc::comptime(inline)]]
Node *generate_answer(void) {
    Type *int_ty = GetType("int");
    Obj *fn = MakeFunction("generated_answer", int_ty);
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(42)));
    return MakeIntLiteral(0);
}

int main(void) {
    generate_answer();
    return generated_answer();
}

// Test ticket #122: file-scope macro call publishes generated parameters.

[[cccc::comptime]]
Node *generate_add(void) {
    Type *int_ty = GetType("int");
    Obj *fn = MakeFunction("generated_add", int_ty);

    FunctionAddParam(fn, "a", int_ty);
    FunctionAddParam(fn, "b", int_ty);

    Node *sum = MakeBinary(NK_ADD, MakeParamRef(fn, "a"),
                             MakeParamRef(fn, "b"));
    FunctionSetBody(fn, MakeReturn(sum));

    return MakeIntLiteral(0);
}

generate_add();

int main(void) {
    return generated_add(20, 22);
}

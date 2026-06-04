// Test ticket #122: explicit pragma macro calls at file scope.

[[jcc::macro]]
_Node *generate_answer(void) {
    _Type *int_ty = _AST_GET_TYPE("int");
    _Obj *fn = _AST_FUNCTION("generated_answer", int_ty);

    _AST_FUNCTION_SET_BODY(fn, _AST_RETURN(_AST_INT_LITERAL(42)));

    return _AST_INT_LITERAL(0);
}

generate_answer();

int main(void) {
    return generated_answer();
}

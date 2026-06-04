// EXPECT_COMPILE_ERROR
// Generated functions must not clobber existing definitions.

int existing_function(void) {
    return 1;
}

[[jcc::macro(inline)]]
_Node *clobber_existing(void) {
    _Type *int_ty = _AST_GET_TYPE("int");
    _Obj *fn = _AST_FUNCTION("existing_function", int_ty);
    _AST_FUNCTION_SET_BODY(fn, _AST_RETURN(_AST_INT_LITERAL(42)));
    return _AST_INT_LITERAL(0);
}

int main(void) {
    return clobber_existing();
}

// Test __jcc_gensym and _GENSYM for collision-safe generated names.

[[jcc::macro(inline)]]
_Node *gensym_test(void) {
    const char *a = _GENSYM("helper");
    const char *b = __jcc_gensym(_VM, "helper");

    int same = 1;
    for (int i = 0; a[i] || b[i]; i++) {
        if (a[i] != b[i]) {
            same = 0;
            break;
        }
    }

    _Type *int_ty = _AST_GET_TYPE("int");
    _Obj *fn_a = _AST_FUNCTION(a, int_ty);
    _Obj *fn_b = _AST_FUNCTION(b, int_ty);
    _AST_FUNCTION_SET_BODY(fn_a, _AST_RETURN(_AST_INT_LITERAL(1)));
    _AST_FUNCTION_SET_BODY(fn_b, _AST_RETURN(_AST_INT_LITERAL(2)));

    if (same || !fn_a || !fn_b || a[0] != 'h' || b[0] != 'h')
        return _AST_INT_LITERAL(1);
    return _AST_INT_LITERAL(42);
}

int main(void) {
    return gensym_test();
}

// JCC_FLAGS: --native
[[jcc::macro(inline)]]
_Node *gen_native_answer(_Node *unused) {
    (void)unused;
    _Obj *fn = _AST_FUNCTION("native_answer", _AST_GET_TYPE("int"));
    _AST_WITH_FN(fn) {
        _AST_FUNCTION_SET_BODY(fn, _QUOTE("return 42;"));
    }
    return _AST_INT_LITERAL(0);
}

int native_answer(void);

int main(void) {
    int dummy = gen_native_answer(0);
    (void)dummy;
    return native_answer();
}

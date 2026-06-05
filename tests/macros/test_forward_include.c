// Test __jcc_forward_include: called from a macro body to register headers
// that should be prepended to serialized output. Duplicate calls for the
// same header must be collapsed to a single #include in the output.

[[jcc::macro]]
void gen_answer(void) {
    _VirtualMachine *vm = _VM;
    // Register the same header twice — output must contain only one entry.
    __jcc_forward_include(vm, "<stddef.h>");
    __jcc_forward_include(vm, "<stddef.h>");
    __jcc_forward_include(vm, "<stdint.h>");
    _Obj *fn = _AST_FUNCTION("get_answer", _AST_GET_TYPE("int"));
    _AST_FUNCTION_SET_BODY(fn, _AST_RETURN(_AST_INT_LITERAL(42)));
}

gen_answer();

int get_answer(void);

int main(void) {
    return get_answer();
}

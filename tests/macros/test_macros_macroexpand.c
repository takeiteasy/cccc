// Test ticket #277: __jcc_macroexpand / _MACROEXPAND reflection API

// A simple macro to expand in tests
[[jcc::macro(inline)]]
_Node *make_answer(void) {
    return __jcc_ast_int_literal(__jcc_get_vm(), 42);
}

// Test 1: macroexpand on a macro-call node expands one level
// _QUOTE("make_answer()") parses to ND_MACRO_CALL; macroexpand executes it.
[[jcc::macro(inline)]]
_Node *test_expand_macro_call(void) {
    _VirtualMachine *vm = __jcc_get_vm();
    _Node *call = __jcc_quote(vm, "make_answer()");
    return __jcc_macroexpand(vm, call);
}

// Test 2: macroexpand on a non-macro node is identity (returns the node as-is)
[[jcc::macro(inline)]]
_Node *test_expand_identity(void) {
    _VirtualMachine *vm = __jcc_get_vm();
    _Node *lit = __jcc_ast_int_literal(vm, 99);
    _Node *result = __jcc_macroexpand(vm, lit);
    // If identity holds, result == lit; return 1 to signal success
    return __jcc_ast_int_literal(vm, result == lit ? 1 : 0);
}

int main(void) {
    // Test 1: expanding a macro-call node should yield the macro's result (42)
    int expanded = test_expand_macro_call();
    if (expanded != 42)
        return 1;

    // Test 2: expanding a non-macro node should be identity
    if (test_expand_identity() != 1)
        return 2;

    return 42;
}

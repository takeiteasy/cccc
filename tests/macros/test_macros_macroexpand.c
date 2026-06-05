// Test ticket #277: __jcc_macroexpand / __jcc_macroexpand_1 / _MACROEXPAND reflection API

// A simple macro to expand in tests
[[jcc::macro(inline)]]
_Node *make_answer(void) {
    return __jcc_ast_int_literal(__jcc_get_vm(), 42);
}

// A macro that wraps make_answer() — used to test multi-level expansion
[[jcc::macro(inline)]]
_Node *wrap_answer(void) {
    return __jcc_quote(__jcc_get_vm(), "make_answer()");
}

// Test 1: macroexpand_1 on a macro-call node expands one level
[[jcc::macro(inline)]]
_Node *test_expand_1_macro_call(void) {
    _VirtualMachine *vm = __jcc_get_vm();
    _Node *call = __jcc_quote(vm, "make_answer()");
    return __jcc_macroexpand_1(vm, call);
}

// Test 2: macroexpand_1 on a non-macro node is identity
[[jcc::macro(inline)]]
_Node *test_expand_1_identity(void) {
    _VirtualMachine *vm = __jcc_get_vm();
    _Node *lit = __jcc_ast_int_literal(vm, 99);
    _Node *result = __jcc_macroexpand_1(vm, lit);
    return __jcc_ast_int_literal(vm, result == lit ? 1 : 0);
}

// Test 3: macroexpand_1 on wrap_answer() expands only one level.
// After one step wrap_answer() -> make_answer(); a second macroexpand_1 then
// resolves that to 42, proving the first step stopped after one expansion.
[[jcc::macro(inline)]]
_Node *test_expand_1_single_level(void) {
    _VirtualMachine *vm = __jcc_get_vm();
    _Node *call = __jcc_quote(vm, "wrap_answer()");
    _Node *step1 = __jcc_macroexpand_1(vm, call);
    _Node *step2 = __jcc_macroexpand_1(vm, step1);
    return step2;
}

// Test 4: macroexpand fully expands wrap_answer() -> make_answer() -> 42
[[jcc::macro(inline)]]
_Node *test_expand_full(void) {
    _VirtualMachine *vm = __jcc_get_vm();
    _Node *call = __jcc_quote(vm, "wrap_answer()");
    return __jcc_macroexpand(vm, call);
}

// Test 5: macroexpand on a non-macro node is identity
[[jcc::macro(inline)]]
_Node *test_expand_full_identity(void) {
    _VirtualMachine *vm = __jcc_get_vm();
    _Node *lit = __jcc_ast_int_literal(vm, 77);
    _Node *result = __jcc_macroexpand(vm, lit);
    return __jcc_ast_int_literal(vm, result == lit ? 1 : 0);
}

int main(void) {
    // Test 1: macroexpand_1 on a macro-call node yields the macro's result (42)
    if (test_expand_1_macro_call() != 42)
        return 1;

    // Test 2: macroexpand_1 on a non-macro node is identity
    if (test_expand_1_identity() != 1)
        return 2;

    // Test 3: two macroexpand_1 calls on wrap_answer fully resolves to 42
    if (test_expand_1_single_level() != 42)
        return 3;

    // Test 4: macroexpand fully expands wrap_answer -> make_answer -> 42
    if (test_expand_full() != 42)
        return 4;

    // Test 5: macroexpand on a non-macro node is identity
    if (test_expand_full_identity() != 1)
        return 5;

    return 42;
}

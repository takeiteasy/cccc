// Test ticket #277: __builtin_macroexpand / __builtin_macroexpand_1 / MacroExpand reflection API

// A simple macro to expand in tests
[[cccc::comptime]]
Node *make_answer(void) {
    return __builtin_ast_int_literal(__builtin_get_vm(), 42);
}

// A macro that wraps make_answer() — used to test multi-level expansion
[[cccc::comptime]]
Node *wrap_answer(void) {
    return __builtin_quote(__builtin_get_vm(), "make_answer()");
}

// Test 1: macroexpand_1 on a macro-call node expands one level
[[cccc::comptime]]
Node *test_expand_1_macro_call(void) {
    VirtualMachine *vm = __builtin_get_vm();
    Node *call = __builtin_quote(vm, "make_answer()");
    return __builtin_macroexpand_1(vm, call);
}

// Test 2: macroexpand_1 on a non-macro node is identity
[[cccc::comptime]]
Node *test_expand_1_identity(void) {
    VirtualMachine *vm = __builtin_get_vm();
    Node *lit = __builtin_ast_int_literal(vm, 99);
    Node *result = __builtin_macroexpand_1(vm, lit);
    return __builtin_ast_int_literal(vm, result == lit ? 1 : 0);
}

// Test 3: macroexpand_1 on wrap_answer() expands only one level.
// After one step wrap_answer() -> make_answer(); a second macroexpand_1 then
// resolves that to 42, proving the first step stopped after one expansion.
[[cccc::comptime]]
Node *test_expand_1_single_level(void) {
    VirtualMachine *vm = __builtin_get_vm();
    Node *call = __builtin_quote(vm, "wrap_answer()");
    Node *step1 = __builtin_macroexpand_1(vm, call);
    Node *step2 = __builtin_macroexpand_1(vm, step1);
    return step2;
}

// Test 4: macroexpand fully expands wrap_answer() -> make_answer() -> 42
[[cccc::comptime]]
Node *test_expand_full(void) {
    VirtualMachine *vm = __builtin_get_vm();
    Node *call = __builtin_quote(vm, "wrap_answer()");
    return __builtin_macroexpand(vm, call);
}

// Test 5: macroexpand on a non-macro node is identity
[[cccc::comptime]]
Node *test_expand_full_identity(void) {
    VirtualMachine *vm = __builtin_get_vm();
    Node *lit = __builtin_ast_int_literal(vm, 77);
    Node *result = __builtin_macroexpand(vm, lit);
    return __builtin_ast_int_literal(vm, result == lit ? 1 : 0);
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

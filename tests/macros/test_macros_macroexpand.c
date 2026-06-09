// Test ticket #277: __cccc_macroexpand / __cccc_macroexpand_1 / $macroexpand reflection API

// A simple macro to expand in tests
[[cccc::comptime(inline)]]
$node_t *make_answer(void) {
    return __cccc_ast_int_literal(__cccc_get_vm(), 42);
}

// A macro that wraps make_answer() — used to test multi-level expansion
[[cccc::comptime(inline)]]
$node_t *wrap_answer(void) {
    return __cccc_quote(__cccc_get_vm(), "make_answer()");
}

// Test 1: macroexpand_1 on a macro-call node expands one level
[[cccc::comptime(inline)]]
$node_t *test_expand_1_macro_call(void) {
    $vm_t *vm = __cccc_get_vm();
    $node_t *call = __cccc_quote(vm, "make_answer()");
    return __cccc_macroexpand_1(vm, call);
}

// Test 2: macroexpand_1 on a non-macro node is identity
[[cccc::comptime(inline)]]
$node_t *test_expand_1_identity(void) {
    $vm_t *vm = __cccc_get_vm();
    $node_t *lit = __cccc_ast_int_literal(vm, 99);
    $node_t *result = __cccc_macroexpand_1(vm, lit);
    return __cccc_ast_int_literal(vm, result == lit ? 1 : 0);
}

// Test 3: macroexpand_1 on wrap_answer() expands only one level.
// After one step wrap_answer() -> make_answer(); a second macroexpand_1 then
// resolves that to 42, proving the first step stopped after one expansion.
[[cccc::comptime(inline)]]
$node_t *test_expand_1_single_level(void) {
    $vm_t *vm = __cccc_get_vm();
    $node_t *call = __cccc_quote(vm, "wrap_answer()");
    $node_t *step1 = __cccc_macroexpand_1(vm, call);
    $node_t *step2 = __cccc_macroexpand_1(vm, step1);
    return step2;
}

// Test 4: macroexpand fully expands wrap_answer() -> make_answer() -> 42
[[cccc::comptime(inline)]]
$node_t *test_expand_full(void) {
    $vm_t *vm = __cccc_get_vm();
    $node_t *call = __cccc_quote(vm, "wrap_answer()");
    return __cccc_macroexpand(vm, call);
}

// Test 5: macroexpand on a non-macro node is identity
[[cccc::comptime(inline)]]
$node_t *test_expand_full_identity(void) {
    $vm_t *vm = __cccc_get_vm();
    $node_t *lit = __cccc_ast_int_literal(vm, 77);
    $node_t *result = __cccc_macroexpand(vm, lit);
    return __cccc_ast_int_literal(vm, result == lit ? 1 : 0);
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

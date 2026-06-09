// Test ticket #286: $@k unquote-splicing into fixed-arity (non-variadic) callees.
// main() returns 42 on success.

// ---- Helper fixed-arity functions --------------------------------------------

int add3(int a, int b, int c) {
    return a + b + c;
}

int add2(int a, int b) {
    return a + b;
}

// ---- Test 1: splice all args into a fixed-arity callee ----------------------
// all three arguments come from a spliced chain; no scalar args.
[[cccc::comptime(inline)]]
$node_t *call_add3($node_t *a, $node_t *b, $node_t *c) {
    $vm_t *vm = __cccc_get_vm();
    $node_t *chain = __cccc_node_list(vm, ($node_t*[]){ a, b, c }, 3);
    return __cccc_quote(vm, "add3($@1)", chain);
}

int test_full_fixed_splice(void) {
    return call_add3(10, 22, 10); // add3(10, 22, 10) == 42
}

// ---- Test 2: scalar prefix + splice tail into fixed-arity callee ------------
// $1 fills the first fixed parameter; $@2 expands to fill the rest.
[[cccc::comptime(inline)]]
$node_t *call_add3_mixed($node_t *a, $node_t *b, $node_t *c) {
    $vm_t *vm = __cccc_get_vm();
    $node_t *chain = __cccc_node_list(vm, ($node_t*[]){ b, c }, 2);
    return __cccc_quote(vm, "add3($1, $@2)", a, chain);
}

int test_prefix_then_fixed_splice(void) {
    return call_add3_mixed(20, 12, 10); // add3(20, 12, 10) == 42
}

// ---- Test 3: splice into a two-parameter fixed callee -----------------------
[[cccc::comptime(inline)]]
$node_t *call_add2($node_t *a, $node_t *b) {
    $vm_t *vm = __cccc_get_vm();
    $node_t *chain = __cccc_node_list(vm, ($node_t*[]){ a, b }, 2);
    return __cccc_quote(vm, "add2($@1)", chain);
}

int test_two_param_fixed_splice(void) {
    return call_add2(21, 21); // add2(21, 21) == 42
}

// ---- Test 4: parameter casts are applied after splice expansion -------------
// Splice double literals into int parameters; the cast should truncate them.
// 10.7 → 10, 21.3 → 21, 10.9 → 10 — correct result is 41 (not 42.9).
[[cccc::comptime(inline)]]
$node_t *call_add3_cast($node_t *a, $node_t *b, $node_t *c) {
    $vm_t *vm = __cccc_get_vm();
    $node_t *chain = __cccc_node_list(vm, ($node_t*[]){ a, b, c }, 3);
    return __cccc_quote(vm, "add3($@1)", chain);
}

int test_cast_on_splice(void) {
    // Each double is truncated to int by the post-splice parameter cast.
    return call_add3_cast(10.7, 21.3, 10.9); // add3(10, 21, 10) == 41
}

// ---- main -------------------------------------------------------------------

int main(void) {
    if (test_full_fixed_splice()        != 42) return 1;
    if (test_prefix_then_fixed_splice() != 42) return 2;
    if (test_two_param_fixed_splice()   != 42) return 3;
    if (test_cast_on_splice()           != 41) return 4;

    return 42;
}

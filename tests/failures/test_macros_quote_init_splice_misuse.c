// EXPECT_COMPILE_ERROR
// Test ticket #195: wrong element count for initializer splice → compile error.
// Providing 1 element for a 2-field struct must be caught at substitution time.

struct Point { int x; int y; };

[[jcc::comptime(inline)]]
$node_t *bad_point($node_t *a) {
    $vm_t *vm = __jcc_get_vm();
    // Only 1 element in chain but struct Point has 2 fields → error.
    $node_t *chain = __jcc_node_list(vm, ($node_t*[]){ a }, 1);
    return $quote("(struct Point){ $@1 }", chain);
}

int main(void) {
    struct Point p = bad_point(1);
    return 42;
}

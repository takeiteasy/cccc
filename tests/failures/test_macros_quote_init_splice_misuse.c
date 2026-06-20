// EXPECT_COMPILE_ERROR
// Test ticket #195: wrong element count for initializer splice → compile error.
// Providing 1 element for a 2-field struct must be caught at substitution time.

struct Point { int x; int y; };

[[cccc::comptime(inline)]]
Node *bad_point(Node *a) {
    VirtualMachine *vm = __builtin_get_vm();
    // Only 1 element in chain but struct Point has 2 fields → error.
    Node *chain = __builtin_node_list(vm, (Node*[]){ a }, 1);
    return Quote("(struct Point){ $@1 }", chain);
}

int main(void) {
    struct Point p = bad_point(1);
    return 42;
}

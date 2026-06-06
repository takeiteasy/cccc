// EXPECT_COMPILE_ERROR
// Test ticket #195: wrong element count for initializer splice → compile error.
// Providing 1 element for a 2-field struct must be caught at substitution time.

struct Point { int x; int y; };

[[jcc::macro(inline)]]
_Node *bad_point(_Node *a) {
    _VirtualMachine *vm = __jcc_get_vm();
    // Only 1 element in chain but struct Point has 2 fields → error.
    _Node *chain = __jcc_node_list(vm, (_Node*[]){ a }, 1);
    return _QUOTE("(struct Point){ $@1 }", chain);
}

int main(void) {
    struct Point p = bad_point(1);
    return 42;
}

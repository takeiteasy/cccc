// EXPECT_COMPILE_ERROR
// Test ticket #1: mixing $N positional and $$ incremental in one template
// should produce a compile-time error.

[[cccc::comptime(inline)]]
$node_t *bad_mix($node_t *a, $node_t *b) {
    $vm_t *vm = __cccc_get_vm();
    // $1 and $$ in the same template — must error
    return __cccc_quote(vm, "$1 + $$", a, b);
}

int main(void) {
    int x = bad_mix(1, 2);
    return x;
}

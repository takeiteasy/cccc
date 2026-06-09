// CCCC_FLAGS: --std=c99 -Wpedantic
// CCCC_REJECT_STDERR: \[-Wpedantic\]
[[cccc::comptime(inline)]]
$node_t *get_42(void) {
    return $int_literal(42);
}
int main(void) { return get_42(); }

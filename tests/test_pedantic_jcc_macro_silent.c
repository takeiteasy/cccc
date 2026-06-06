// JCC_FLAGS: -std=c99 -Wpedantic
// JCC_REJECT_STDERR: \[-Wpedantic\]
[[jcc::comptime(inline)]]
$node_t *get_42(void) {
    return $int_literal(42);
}
int main(void) { return get_42(); }

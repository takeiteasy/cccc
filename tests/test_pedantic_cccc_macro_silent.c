// CCCC_FLAGS: --std=c99 -Wpedantic
// CCCC_REJECT_STDERR: \[-Wpedantic\]
[[cccc::comptime(inline)]]
Node *get_42(void) {
    return MakeIntLiteral(42);
}
int main(void) { return get_42(); }

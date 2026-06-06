// EXPECT_COMPILE_ERROR
// Ordinary runtime functions are not lazily compiled into macro bytecode.

int runtime_helper(int n) {
    return n + 1;
}

[[jcc::macro(inline)]]
$node_t *uses_runtime_helper(void) {
    return $int_literal(runtime_helper(41));
}

int main(void) {
    return uses_runtime_helper();
}

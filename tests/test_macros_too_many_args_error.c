// EXPECT_COMPILE_ERROR
[[jcc::comptime(inline)]]
$node_t *ninth($node_t *a0, $node_t *a1, $node_t *a2, $node_t *a3,
             $node_t *a4, $node_t *a5, $node_t *a6, $node_t *a7,
             $node_t *a8) {
    return a8;
}

int main() { return ninth(1, 2, 3, 4, 5, 6, 7, 8, 42); }

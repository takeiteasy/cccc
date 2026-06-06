// EXPECT_COMPILE_ERROR
// Ticket #284: variadic tails are unbounded, but fixed parameters are still
// capped at 8.

[[jcc::comptime(inline)]]
$node_t *too_many_fixed($node_t *a0, $node_t *a1, $node_t *a2, $node_t *a3,
                        $node_t *a4, $node_t *a5, $node_t *a6, $node_t *a7,
                        $node_t *a8, ...) {
    return a8;
}

int main(void) {
    return too_many_fixed(1, 2, 3, 4, 5, 6, 7, 8, 9);
}

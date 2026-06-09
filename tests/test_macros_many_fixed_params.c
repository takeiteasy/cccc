// Ticket #287: compile-time macros can receive fixed parameters beyond 8.

[[cccc::comptime(inline)]]
$node_t *ninth_arg($node_t *a0, $node_t *a1, $node_t *a2, $node_t *a3,
                   $node_t *a4, $node_t *a5, $node_t *a6, $node_t *a7,
                   $node_t *a8) {
    return a8;
}

[[cccc::comptime(inline)]]
$node_t *fixed9_plus_tail($node_t *a0, $node_t *a1, $node_t *a2,
                          $node_t *a3, $node_t *a4, $node_t *a5,
                          $node_t *a6, $node_t *a7, $node_t *a8, ...) {
    return $binary(nk_add, a8, _AST_VARARG_AT(0));
}

int main(void) {
    int a = ninth_arg(1, 2, 3, 4, 5, 6, 7, 8, 9);
    int b = fixed9_plus_tail(1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
    return a == 9 && b == 19 ? 42 : 1;
}

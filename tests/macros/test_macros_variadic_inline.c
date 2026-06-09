// Test ticket #284: variadic inline macros receive an unbounded AST tail.

[[cccc::comptime(inline)]]
$node_t *sum_nodes(...) {
    int count = _AST_VARARG_COUNT();
    if (count == 0)
        return $int_literal(0);

    $node_t *acc = _AST_VARARG_AT(0);
    for (int i = 1; i < count; i = i + 1)
        acc = $binary(nk_add, acc, _AST_VARARG_AT(i));
    return acc;
}

[[cccc::comptime(inline)]]
$node_t *add_tail($node_t *base, ...) {
    $node_t *acc = base;
    for (int i = 0; i < _AST_VARARG_COUNT(); i = i + 1)
        acc = $binary(nk_add, acc, _AST_VARARG_AT(i));
    return acc;
}

int main(void) {
    if (sum_nodes(1, 2, 3, 4, 5, 6, 7, 8, 9, 10) != 55)
        return 1;
    if (add_tail(10, 1, 2, 3, 4, 5, 6, 7, 8, 9) != 55)
        return 2;
    return 42;
}

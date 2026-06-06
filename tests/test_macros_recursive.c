// Test pragma macros calling other pragma macros from compile-time bytecode.
// This intentionally omits <reflection.h>; pragma macro compilation includes it
// privately.

[[jcc::macro(inline)]]
$node_t *forward_add_two($node_t *x) {
    return add_two_later(x);
}

[[jcc::macro(inline)]]
$node_t *add_one($node_t *x) {
    return $binary(nk_add, x, $int_literal(1));
}

[[jcc::macro(inline)]]
$node_t *add_two_later($node_t *x) {
    return add_one(add_one(x));
}

[[jcc::macro(inline)]]
$node_t *chain_top($node_t *x) {
    return add_one(add_two_later(x));
}

[[jcc::macro(inline)]]
$node_t *mutual_even(int n, $node_t *x) {
    if (n <= 0)
        return x;
    return mutual_odd(n - 1, $binary(nk_add, x, $int_literal(1)));
}

[[jcc::macro(inline)]]
$node_t *mutual_odd(int n, $node_t *x) {
    if (n <= 0)
        return x;
    return mutual_even(n - 1, $binary(nk_add, x, $int_literal(1)));
}

[[jcc::macro(inline)]]
$node_t *mutual_add_four($node_t *x) {
    return mutual_even(4, x);
}

int main(void) {
    if (forward_add_two(40) != 42)
        return 1;

    if (chain_top(10) != 13)
        return 2;

    if (mutual_add_four(38) != 42)
        return 3;

    return 42;
}

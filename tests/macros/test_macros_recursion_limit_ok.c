// Test finite nested macro expansion under the default recursion limit.

[[jcc::comptime(inline)]]
$node_t *finish_step($node_t *x) {
    return $binary(nk_add, x, $int_literal(1));
}

[[jcc::comptime(inline)]]
$node_t *start_step($node_t *x) {
    return $quote("finish_step($1)", x);
}

int main(void) {
    return start_step(41);
}

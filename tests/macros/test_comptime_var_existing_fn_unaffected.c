// Ticket #188: [[cccc::comptime]] function declarations still work alongside
// comptime variable declarations — regression guard.

[[cccc::comptime]]
int double_it(int n) { return n * 2; }

[[cccc::comptime]]
int answer = 21;

[[cccc::comptime(inline)]]
$node_t *get_answer(void) {
    return $int_literal($get_comptime_int("answer") * 2);
}

[[cccc::comptime(inline)]]
$node_t *use_helper($node_t *x) {
    return $binary(nk_add, x, $int_literal(double_it(1)));
}

int main(void) {
    if (get_answer() != 42)
        return 1;
    // use_helper(40) => 40 + double_it(1) => 40 + 2 = 42
    if (use_helper(40) != 42)
        return 2;
    return 42;
}

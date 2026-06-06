// Ticket #191: comptime variable initializer references another comptime
// variable (cross-var reference, evaluated in declaration order).

[[jcc::comptime]]
int compute_base(void) { return 7; }

[[jcc::comptime]]
int a = compute_base() * 3;   // a == 21

[[jcc::comptime]]
int b = a * 2;                 // b == 42  (references a)

[[jcc::comptime(inline)]]
$node_t *get_a(void) {
    return $int_literal($get_comptime_int("a"));
}

[[jcc::comptime(inline)]]
$node_t *get_b(void) {
    return $int_literal($get_comptime_int("b"));
}

int main(void) {
    if (get_a() != 21)
        return 1;
    if (get_b() != 42)
        return 2;
    return 42;
}

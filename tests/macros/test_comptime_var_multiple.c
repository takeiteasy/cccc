// Ticket #188: multiple comptime variables of different types, declared in any
// order relative to macros that use them.

[[cccc::comptime]]
int base = 20;

[[cccc::comptime(inline)]]
$node_t *doubled(void) {
    return $int_literal($get_comptime_int("base") * 2);
}

[[cccc::comptime]]
int bonus = 2;

[[cccc::comptime(inline)]]
$node_t *total(void) {
    int64_t b = $get_comptime_int("base");
    int64_t n = $get_comptime_int("bonus");
    return $int_literal(b + n);
}

int main(void) {
    if (doubled() != 40)
        return 1;
    if (total() != 22)
        return 2;
    return 42;
}

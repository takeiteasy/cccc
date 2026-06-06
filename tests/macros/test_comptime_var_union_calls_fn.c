// Ticket #192: union comptime var whose initializer calls a comptime function.
// Exercises the (union Tag){ ... } compound-literal cast branch.

[[jcc::comptime]]
int compute_value(void) { return 0x12345678; }

[[jcc::comptime]]
union Data { int i; unsigned char bytes[4]; } data = { compute_value() };

[[jcc::macro(inline)]]
$node_t *get_value(void) {
    return $get_comptime_member("data", "i");
}

int main(void) {
    if (get_value() != 0x12345678)
        return 1;
    return 42;
}

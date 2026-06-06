// Ticket #191: comptime float variable initializer calls a comptime function.

[[jcc::comptime]]
double get_scale_factor(void) { return 1.5; }

[[jcc::comptime]]
double area = get_scale_factor() * 200.0;

[[jcc::macro(inline)]]
$node_t *get_area_int(void) {
    // Cast to int for exact comparison: 1.5 * 200.0 == 300.
    return $int_literal((int)$get_comptime_float("area"));
}

int main(void) {
    if (get_area_int() != 300)
        return 1;
    return 42;
}

// Ticket #191: comptime float variable initializer calls a comptime function.

[[cccc::comptime]]
double get_scale_factor(void) {
    return 1.5;
}

[[cccc::comptime]]
double area = get_scale_factor() * 200.0;

[[cccc::comptime]]
Node *get_area_int(void) {
    // Cast to int for exact comparison: 1.5 * 200.0 == 300.
    return MakeIntLiteral((int)GetComptimeFloat("area"));
}

int main(void) {
    if (get_area_int() != 300)
        return 1;
    return 42;
}

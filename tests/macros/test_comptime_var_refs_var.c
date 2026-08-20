// Ticket #191: comptime variable initializer references another comptime
// variable (cross-var reference, evaluated in declaration order).

[[cccc::comptime]]
int compute_base(void) {
    return 7;
}

[[cccc::comptime]]
int a = compute_base() * 3; // a == 21

[[cccc::comptime]]
int b = a * 2;              // b == 42  (references a)

[[cccc::comptime]]
Node *get_a(void) {
    return MakeIntLiteral(GetComptimeInt("a"));
}

[[cccc::comptime]]
Node *get_b(void) {
    return MakeIntLiteral(GetComptimeInt("b"));
}

int main(void) {
    if (get_a() != 21)
        return 1;
    if (get_b() != 42)
        return 2;
    return 42;
}

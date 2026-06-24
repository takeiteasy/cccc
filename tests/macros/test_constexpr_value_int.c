// CCCC_FLAGS: --std=c23
// Ticket #189: integer constexpr variable readable from a macro.

constexpr int LIMIT = 100;
constexpr int NEG   = -7;

[[cccc::comptime]]
Node *get_limit(void) {
    return GetConstexprValue("LIMIT");
}

[[cccc::comptime]]
Node *get_neg(void) {
    return GetConstexprValue("NEG");
}

int main(void) {
    if (get_limit() != 100)
        return 1;
    if (get_neg() != -7)
        return 2;
    return 42;
}

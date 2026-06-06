// JCC_FLAGS: -std=c23
// Ticket #189: integer constexpr variable readable from a macro.

constexpr int LIMIT = 100;
constexpr int NEG   = -7;

[[jcc::macro(inline)]]
$node_t *get_limit(void) {
    return $get_constexpr_value("LIMIT");
}

[[jcc::macro(inline)]]
$node_t *get_neg(void) {
    return $get_constexpr_value("NEG");
}

int main(void) {
    if (get_limit() != 100)
        return 1;
    if (get_neg() != -7)
        return 2;
    return 42;
}

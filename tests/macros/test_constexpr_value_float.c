// CCCC_FLAGS: --std=c23
// Ticket #189: float constexpr variable readable from a macro.

constexpr double SCALE = 3.5;
constexpr float  HALF  = 0.5f;

[[cccc::comptime(inline)]]
Node *get_scale(void) {
    return GetConstexprValue("SCALE");
}

[[cccc::comptime(inline)]]
Node *get_half(void) {
    return GetConstexprValue("HALF");
}

int main(void) {
    // 3.5 * 2 == 7 — use integer cast to avoid float equality issues
    if ((int)(get_scale() * 2.0) != 7)
        return 1;
    // 0.5 * 4 == 2
    if ((int)(get_half() * 4.0f) != 2)
        return 2;
    return 42;
}

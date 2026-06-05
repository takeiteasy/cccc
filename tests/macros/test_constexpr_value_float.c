// JCC_FLAGS: -std=c23
// Ticket #189: float constexpr variable readable from a macro.

constexpr double SCALE = 3.5;
constexpr float  HALF  = 0.5f;

[[jcc::macro(inline)]]
_Node *get_scale(void) {
    return _AST_GET_CONSTEXPR_VALUE("SCALE");
}

[[jcc::macro(inline)]]
_Node *get_half(void) {
    return _AST_GET_CONSTEXPR_VALUE("HALF");
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

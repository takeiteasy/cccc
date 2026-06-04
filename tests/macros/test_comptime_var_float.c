// Ticket #188: float comptime variable readable from a macro.

[[jcc::comptime]]
double scale = 2.5;

[[jcc::macro(inline)]]
_Node *get_scale(void) {
    return _AST_GET_COMPTIME_VAR("scale");
}

[[jcc::macro(inline)]]
_Node *check_scale(void) {
    double s = _AST_GET_COMPTIME_FLOAT("scale");
    // scale * 4 == 10 — check via int cast to avoid float equality issues
    return _AST_INT_LITERAL((int)(s * 4));
}

int main(void) {
    // get_scale() returns the float literal 2.5; cast to int gives 2
    if ((int)get_scale() != 2)
        return 1;
    if (check_scale() != 10)
        return 2;
    return 42;
}

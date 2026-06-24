// Ticket #188: float comptime variable readable from a macro.

[[cccc::comptime]]
double scale = 2.5;

[[cccc::comptime]]
Node *get_scale(void) {
    return GetComptimeVar("scale");
}

[[cccc::comptime]]
Node *check_scale(void) {
    double s = GetComptimeFloat("scale");
    // scale * 4 == 10 — check via int cast to avoid float equality issues
    return MakeIntLiteral((int)(s * 4));
}

int main(void) {
    // get_scale() returns the float literal 2.5; cast to int gives 2
    if ((int)get_scale() != 2)
        return 1;
    if (check_scale() != 10)
        return 2;
    return 42;
}

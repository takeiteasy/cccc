// Ticket #188: multiple comptime variables of different types, declared in any
// order relative to macros that use them.

[[cccc::comptime]]
int base = 20;

[[cccc::comptime(inline)]]
Node *doubled(void) {
    return MakeIntLiteral(GetComptimeInt("base") * 2);
}

[[cccc::comptime]]
int bonus = 2;

[[cccc::comptime(inline)]]
Node *total(void) {
    int64_t b = GetComptimeInt("base");
    int64_t n = GetComptimeInt("bonus");
    return MakeIntLiteral(b + n);
}

int main(void) {
    if (doubled() != 40)
        return 1;
    if (total() != 22)
        return 2;
    return 42;
}

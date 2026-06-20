// Ticket #188: integer comptime variable readable from a macro.

[[cccc::comptime]]
int magic = 42;

[[cccc::comptime(inline)]]
Node *get_magic(void) {
    return GetComptimeVar("magic");
}

[[cccc::comptime(inline)]]
Node *get_magic_int(void) {
    return MakeIntLiteral(GetComptimeInt("magic"));
}

int main(void) {
    if (get_magic() != 42)
        return 1;
    if (get_magic_int() != 42)
        return 2;
    return 42;
}

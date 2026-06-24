// Test macro hygiene: call-site variable and typedef lookup.

[[cccc::comptime]]
Node *read_value(void) {
    return MakeVarRef("value");
}

[[cccc::comptime]]
Node *read_value_via_quote(void) {
    return Quote("read_value()");
}

[[cccc::comptime]]
Node *local_alias_size(void) {
    Type *ty = FindType("LocalAlias");
    if (!ty)
        return MakeIntLiteral(0);
    return MakeIntLiteral(TypeSize(ty));
}

int value = 5;

int main(void) {
    int value = 40;

    {
        typedef long LocalAlias;
        int value = 42;

        if (read_value() != 42)
            return 1;
        if (local_alias_size() != 8)
            return 2;
        if (read_value_via_quote() != 42)
            return 3;
    }

    if (read_value() != 40)
        return 4;

    return 42;
}

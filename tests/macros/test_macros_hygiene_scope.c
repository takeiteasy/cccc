// Test macro hygiene: call-site variable and typedef lookup.

[[cccc::comptime(inline)]]
$node_t *read_value(void) {
    return $var_ref("value");
}

[[cccc::comptime(inline)]]
$node_t *read_value_via_quote(void) {
    return $quote("read_value()");
}

[[cccc::comptime(inline)]]
$node_t *local_alias_size(void) {
    $type_t *ty = $find_type("LocalAlias");
    if (!ty)
        return $int_literal(0);
    return $int_literal($type_size(ty));
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

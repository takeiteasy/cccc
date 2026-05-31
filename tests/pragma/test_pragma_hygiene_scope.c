// Test macro hygiene: call-site variable and typedef lookup.

#pragma macro
_Node *read_value(void) {
    return _AST_VAR_REF("value");
}

#pragma macro
_Node *read_value_via_quote(void) {
    return _QUOTE("read_value()");
}

#pragma macro
_Node *local_alias_size(void) {
    _Type *ty = _AST_FIND_TYPE("LocalAlias");
    if (!ty)
        return _AST_INT_LITERAL(0);
    return _AST_INT_LITERAL(_AST_TYPE_SIZE(ty));
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

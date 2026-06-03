// Ticket #188: integer comptime variable readable from a macro.

[[jcc::comptime]]
int magic = 42;

[[jcc::macro]]
_Node *get_magic(void) {
    return _AST_GET_COMPTIME_VAR("magic");
}

[[jcc::macro]]
_Node *get_magic_int(void) {
    return _AST_INT_LITERAL(_AST_GET_COMPTIME_INT("magic"));
}

int main(void) {
    if (get_magic() != 42)
        return 1;
    if (get_magic_int() != 42)
        return 2;
    return 42;
}

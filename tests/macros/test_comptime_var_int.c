// Ticket #188: integer comptime variable readable from a macro.

[[cccc::comptime]]
int magic = 42;

[[cccc::comptime(inline)]]
$node_t *get_magic(void) {
    return $get_comptime_var("magic");
}

[[cccc::comptime(inline)]]
$node_t *get_magic_int(void) {
    return $int_literal($get_comptime_int("magic"));
}

int main(void) {
    if (get_magic() != 42)
        return 1;
    if (get_magic_int() != 42)
        return 2;
    return 42;
}

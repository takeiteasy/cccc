// Ticket #191: comptime variable initializer calls a comptime function that
// is defined AFTER the variable (forward reference). All comptime functions
// are compiled before any initializers run, so forward refs are supported.

[[cccc::comptime]]
int items = get_item_count();   // forward ref — get_item_count defined below

[[cccc::comptime]]
int get_item_count(void) { return 6; }

[[cccc::comptime(inline)]]
Node *get_items(void) {
    return MakeIntLiteral(GetComptimeInt("items"));
}

int main(void) {
    // items == get_item_count() == 6; 6 * 7 == 42
    if (get_items() != 6)
        return 1;
    return 42;
}

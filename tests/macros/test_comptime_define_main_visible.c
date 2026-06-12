// Ticket #283: #define from the main file must remain visible inside comptime
// bodies after macro table isolation. The snapshot is taken after the main
// preprocessing pass, so user-defined macros are part of it.
#define FROM_MAIN 77

[[cccc::comptime]] int get_val(void) { return FROM_MAIN; }

[[cccc::comptime(inline)]]
$node_t *call_get(void) { return $int_literal(get_val()); }

int main(void) { return call_get() == 77 ? 42 : 1; }

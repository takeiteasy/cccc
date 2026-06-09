// Ticket #193: anonymous aggregate comptime vars with non-constant
// initializers use a typeof(var) compound-literal assignment.

[[cccc::comptime]]
int compute_width(void) { return 1024; }

[[cccc::comptime]]
int compute_value(void) { return 0x1234; }

[[cccc::comptime]]
struct { int width; int height; } dims = { compute_width(), 768 };

[[cccc::comptime]]
union { int i; unsigned char bytes[4]; } data = { compute_value() };

[[cccc::comptime(inline)]]
$node_t *get_width(void) {
    return $get_comptime_member("dims", "width");
}

[[cccc::comptime(inline)]]
$node_t *get_height(void) {
    return $get_comptime_member("dims", "height");
}

[[cccc::comptime(inline)]]
$node_t *get_value(void) {
    return $get_comptime_member("data", "i");
}

int main(void) {
    if (get_width() != 1024)
        return 1;
    if (get_height() != 768)
        return 2;
    if (get_value() != 0x1234)
        return 3;
    return 42;
}

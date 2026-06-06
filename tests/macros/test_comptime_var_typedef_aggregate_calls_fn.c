// Ticket #193: typedef'd aggregate comptime vars with non-constant
// initializers use the typedef name in the compound-literal assignment.

typedef struct {
    int width;
    int height;
} Dims;

typedef union {
    int i;
    unsigned char bytes[4];
} Data;

[[jcc::comptime]]
int compute_width(void) { return 1280; }

[[jcc::comptime]]
int compute_value(void) { return 0x5678; }

[[jcc::comptime]]
Dims dims = { compute_width(), 720 };

[[jcc::comptime]]
Data data = { compute_value() };

[[jcc::macro(inline)]]
$node_t *get_width(void) {
    return $get_comptime_member("dims", "width");
}

[[jcc::macro(inline)]]
$node_t *get_height(void) {
    return $get_comptime_member("dims", "height");
}

[[jcc::macro(inline)]]
$node_t *get_value(void) {
    return $get_comptime_member("data", "i");
}

int main(void) {
    if (get_width() != 1280)
        return 1;
    if (get_height() != 720)
        return 2;
    if (get_value() != 0x5678)
        return 3;
    return 42;
}

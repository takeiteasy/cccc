// Ticket #192: struct comptime variable whose initializer calls a comptime
// function. The aggregate { compute_width(), 480 } is non-constant, so it
// cannot be evaluated via init_data; __jcc_comptime_init emits a compound-
// literal assignment: dims = (struct Dims){ compute_width(), 480 };

[[jcc::comptime]]
int compute_width(void) { return 1920; }

[[jcc::comptime]]
struct Dims { int width; int height; } dims = { compute_width(), 1080 };

[[jcc::macro(inline)]]
$node_t *get_area(void) {
    $node_t *w = $get_comptime_member("dims", "width");
    $node_t *h = $get_comptime_member("dims", "height");
    return $binary(nk_mul, w, h);
}

[[jcc::macro(inline)]]
$node_t *get_width(void) {
    return $get_comptime_member("dims", "width");
}

int main(void) {
    if (get_width() != 1920)
        return 1;
    if (get_area() != 1920 * 1080)
        return 2;
    return 42;
}

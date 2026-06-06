// Ticket #188: struct comptime variable with readable members.

[[jcc::comptime]]
struct Dims { int width; int height; int depth; } dims = { 640, 480, 3 };

[[jcc::macro(inline)]]
$node_t *get_area(void) {
    // width * height at compile time
    $node_t *w = $get_comptime_member("dims", "width");
    $node_t *h = $get_comptime_member("dims", "height");
    return $binary(nk_mul, w, h);
}

[[jcc::macro(inline)]]
$node_t *get_depth(void) {
    return $get_comptime_member("dims", "depth");
}

int main(void) {
    if (get_area() != 640 * 480)
        return 1;
    if (get_depth() != 3)
        return 2;
    return 42;
}

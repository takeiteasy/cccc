// Ticket #192: struct comptime variable whose initializer calls a comptime
// function. The aggregate { compute_width(), 480 } is non-constant, so it
// cannot be evaluated via init_data; __jcc_comptime_init emits a compound-
// literal assignment: dims = (struct Dims){ compute_width(), 480 };

[[jcc::comptime]]
int compute_width(void) { return 1920; }

[[jcc::comptime]]
struct Dims { int width; int height; } dims = { compute_width(), 1080 };

[[jcc::macro(inline)]]
_Node *get_area(void) {
    _Node *w = _AST_GET_COMPTIME_MEMBER("dims", "width");
    _Node *h = _AST_GET_COMPTIME_MEMBER("dims", "height");
    return _AST_BINARY(_MUL, w, h);
}

[[jcc::macro(inline)]]
_Node *get_width(void) {
    return _AST_GET_COMPTIME_MEMBER("dims", "width");
}

int main(void) {
    if (get_width() != 1920)
        return 1;
    if (get_area() != 1920 * 1080)
        return 2;
    return 42;
}

// Ticket #188: struct comptime variable with readable members.

[[jcc::comptime]]
struct Dims { int width; int height; int depth; } dims = { 640, 480, 3 };

[[jcc::macro]]
_Node *get_area(void) {
    // width * height at compile time
    _Node *w = _AST_GET_COMPTIME_MEMBER("dims", "width");
    _Node *h = _AST_GET_COMPTIME_MEMBER("dims", "height");
    return _AST_BINARY(_MUL, w, h);
}

[[jcc::macro]]
_Node *get_depth(void) {
    return _AST_GET_COMPTIME_MEMBER("dims", "depth");
}

int main(void) {
    if (get_area() != 640 * 480)
        return 1;
    if (get_depth() != 3)
        return 2;
    return 42;
}

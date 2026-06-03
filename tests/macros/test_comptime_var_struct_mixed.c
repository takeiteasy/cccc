// Ticket #192: struct comptime var with a mix of constant and function-call
// members, plus a cross-var reference to a prior scalar comptime var.

[[jcc::comptime]]
int base_width(void) { return 320; }

// Scalar comptime var used as cross-var reference in the struct initializer.
[[jcc::comptime]]
int scale = 2;

// Struct initializer: first member is a function call, second is a constant,
// third references the scalar comptime var 'scale' (cross-var ref).
[[jcc::comptime]]
struct Config {
    int width;
    int height;
    int scale;
} cfg = { base_width(), 240, scale };

[[jcc::macro]]
_Node *get_width(void) {
    return _AST_GET_COMPTIME_MEMBER("cfg", "width");
}

[[jcc::macro]]
_Node *get_height(void) {
    return _AST_GET_COMPTIME_MEMBER("cfg", "height");
}

[[jcc::macro]]
_Node *get_scale(void) {
    return _AST_GET_COMPTIME_MEMBER("cfg", "scale");
}

int main(void) {
    if (get_width() != 320)
        return 1;
    if (get_height() != 240)
        return 2;
    if (get_scale() != 2)
        return 3;
    return 42;
}

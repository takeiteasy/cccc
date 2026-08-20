// Ticket #309: static function-pointer tables inside comptime functions.

[[cccc::comptime]]
int handler_a(void) {
    return 10;
}

[[cccc::comptime]]
int handler_b(void) {
    return 20;
}

[[cccc::comptime]]
int dispatch(int i) {
    static int (*const table[])(void) = {handler_a, handler_b};
    return table[i]();
}

[[cccc::comptime]]
Node *dispatch_ok(void) {
    if (dispatch(0) != 10)
        return MakeIntLiteral(1);
    if (dispatch(1) != 20)
        return MakeIntLiteral(2);
    return MakeIntLiteral(42);
}

int main(void) {
    return dispatch_ok();
}

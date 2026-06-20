// Ticket #309: a comptime function-pointer table must not pollute the
// runtime program's own function-pointer table relocations.

[[cccc::comptime]]
int chandler_a(void) { return 10; }

[[cccc::comptime]]
int chandler_b(void) { return 20; }

[[cccc::comptime]]
int cdispatch(int i) {
    static int (*const table[])(void) = {chandler_a, chandler_b};
    return table[i]();
}

[[cccc::comptime(inline)]]
Node *cdispatch_ok(void) {
    if (cdispatch(0) != 10)
        return MakeIntLiteral(1);
    if (cdispatch(1) != 20)
        return MakeIntLiteral(2);
    return MakeIntLiteral(0);
}

int rhandler_a(void) { return 100; }
int rhandler_b(void) { return 200; }

static int (*const rtable[])(void) = {rhandler_a, rhandler_b};

int main(void) {
    int c = cdispatch_ok();
    if (c != 0)
        return c;

    if (rtable[0]() != 100)
        return 3;
    if (rtable[1]() != 200)
        return 4;

    return 42;
}

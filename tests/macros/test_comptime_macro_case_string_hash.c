// A comptime macro that hashes a string literal is usable in a case label.

[[cccc::comptime]]
Node *H(Node *s) {
    const char *text = StringLiteralValue(s);
    if (!text)
        MacroErrorAt(0, "H expects a string literal");
    int h = 0;
    for (const char *p = text; *p; p++)
        h = h * 31 + *p;
    return MakeIntLiteral(h);
}

static int hash(const char *s) {
    int h = 0;
    for (; *s; s++)
        h = h * 31 + *s;
    return h;
}

static int which(const char *s) {
    int h = hash(s);
    // TODO(#1612): switch (hash(s)) selects the wrong case.
    switch (h) {
        case H("one"):
            return 1;
        case H("two"):
            return 2;
        default:
            return 0;
    }
}

int main(void) {
    if (which("one") != 1 || which("two") != 2 || which("three") != 0)
        return 1;
    return 42;
}

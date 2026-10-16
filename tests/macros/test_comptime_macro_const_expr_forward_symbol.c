// A comptime macro used in a constant position compiles the comptime program
// at that point in the parse; a later comptime function can still reflect a
// type declared after it.

[[cccc::comptime]]
Node *ONE(void) {
    return MakeIntLiteral(1);
}

static int pick(int v) {
    switch (v) {
        case ONE():
            return 10;
    }
    return 0;
}

[[cccc::comptime]]
Node *late_size(void) {
    return MakeIntLiteral(TypeSize($Late));
}

struct Late {
    int a;
    int b;
};

int late_bytes = late_size();

int main(void) {
    if (pick(1) != 10)
        return 1;
    if (late_bytes != (int)sizeof(struct Late))
        return 2;
    return 42;
}

// Ticket #192: union comptime var whose initializer calls a comptime function.
// Exercises the (union Tag){ ... } compound-literal cast branch.

[[cccc::comptime]]
int compute_value(void) { return 0x12345678; }

[[cccc::comptime]]
union Data { int i; unsigned char bytes[4]; } data = { compute_value() };

[[cccc::comptime(inline)]]
Node *get_value(void) {
    return GetComptimeMember("data", "i");
}

int main(void) {
    if (get_value() != 0x12345678)
        return 1;
    return 42;
}

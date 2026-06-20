// Ticket #193: typedef'd aggregate comptime vars with non-constant
// initializers use the typedef name in the compound-literal assignment.

typedef struct {
    int width;
    int height;
} Dims;

typedef union {
    int i;
    unsigned char bytes[4];
} Data;

[[cccc::comptime]]
int compute_width(void) { return 1280; }

[[cccc::comptime]]
int compute_value(void) { return 0x5678; }

[[cccc::comptime]]
Dims dims = { compute_width(), 720 };

[[cccc::comptime]]
Data data = { compute_value() };

[[cccc::comptime(inline)]]
Node *get_width(void) {
    return GetComptimeMember("dims", "width");
}

[[cccc::comptime(inline)]]
Node *get_height(void) {
    return GetComptimeMember("dims", "height");
}

[[cccc::comptime(inline)]]
Node *get_value(void) {
    return GetComptimeMember("data", "i");
}

int main(void) {
    if (get_width() != 1280)
        return 1;
    if (get_height() != 720)
        return 2;
    if (get_value() != 0x5678)
        return 3;
    return 42;
}

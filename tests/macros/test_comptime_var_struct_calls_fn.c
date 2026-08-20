// Ticket #192: struct comptime variable whose initializer calls a comptime
// function. The aggregate { compute_width(), 480 } is non-constant, so it
// cannot be evaluated via init_data; __builtin_comptime_init emits a compound-
// literal assignment: dims = (struct Dims){ compute_width(), 480 };

[[cccc::comptime]]
int compute_width(void) {
    return 1920;
}

[[cccc::comptime]] struct Dims {
    int width;
    int height;
} dims = {compute_width(), 1080};

[[cccc::comptime]]
Node *get_area(void) {
    Node *w = GetComptimeMember("dims", "width");
    Node *h = GetComptimeMember("dims", "height");
    return MakeBinary(NK_MUL, w, h);
}

[[cccc::comptime]]
Node *get_width(void) {
    return GetComptimeMember("dims", "width");
}

int main(void) {
    if (get_width() != 1920)
        return 1;
    if (get_area() != 1920 * 1080)
        return 2;
    return 42;
}

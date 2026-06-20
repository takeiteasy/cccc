// Ticket #188: struct comptime variable with readable members.

[[cccc::comptime]]
struct Dims { int width; int height; int depth; } dims = { 640, 480, 3 };

[[cccc::comptime(inline)]]
Node *get_area(void) {
    // width * height at compile time
    Node *w = GetComptimeMember("dims", "width");
    Node *h = GetComptimeMember("dims", "height");
    return MakeBinary(NK_MUL, w, h);
}

[[cccc::comptime(inline)]]
Node *get_depth(void) {
    return GetComptimeMember("dims", "depth");
}

int main(void) {
    if (get_area() != 640 * 480)
        return 1;
    if (get_depth() != 3)
        return 2;
    return 42;
}

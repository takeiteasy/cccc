#include <stdio.h>

// A call-site macro: the call is replaced by the returned template.
@comptime Node *square(Node *x) { return Quote("($1) * ($1)", x); }

// A comptime function in a global initializer folds to a constant.
@comptime Node *pow2(Node *k) { return Quote("1 << ($1)", k); }

// A file-scope macro that emits real declarations into the program.
@comptime Node *emit_vec2(void) {
    return Quote("{ struct Vec2 { int x, y; };"
                 "  int vec2_dot(struct Vec2 a, struct Vec2 b) {"
                 "      return a.x * b.x + a.y * b.y; } }");
}
emit_vec2();

int TABLE_SIZE = pow2(8);                  // 256, computed at compile time

int main(void) {
    struct Vec2 a = { 3, 4 };
    struct Vec2 b = { square(2), 5 };      // square(2) -> (2) * (2)
    printf("%d %d\n", TABLE_SIZE, vec2_dot(a, b));   // 256 32
    return 0;
}

// Discovered and run by `cccc --testing comptime.c`.
@test void dot_product(void) {
    struct Vec2 a = { 3, 4 }, b = { square(2), 5 };
    AssertEq(vec2_dot(a, b), 32);
}

// Run by `cccc --build comptime.c`.
@build_target BuildTarget *demo(Builder *ctx) {
    BuildTarget *t = CcccExecutable(ctx, "demo");
    AddSource(t, "comptime.c");
    return t;
}
